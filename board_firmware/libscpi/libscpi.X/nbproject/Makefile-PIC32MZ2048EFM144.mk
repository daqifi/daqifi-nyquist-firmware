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
ifeq "$(wildcard nbproject/Makefile-local-PIC32MZ2048EFM144.mk)" "nbproject/Makefile-local-PIC32MZ2048EFM144.mk"
include nbproject/Makefile-local-PIC32MZ2048EFM144.mk
endif
endif

# Environment
MKDIR=gnumkdir -p
RM=rm -f 
MV=mv 
CP=cp 

# Macros
CND_CONF=PIC32MZ2048EFM144
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
IMAGE_TYPE=debug
OUTPUT_SUFFIX=a
DEBUGGABLE_SUFFIX=
FINAL_IMAGE=${DISTDIR}/libscpi.X.${OUTPUT_SUFFIX}
else
IMAGE_TYPE=production
OUTPUT_SUFFIX=a
DEBUGGABLE_SUFFIX=
FINAL_IMAGE=${DISTDIR}/libscpi.X.${OUTPUT_SUFFIX}
endif

ifeq ($(COMPARE_BUILD), true)
COMPARISON_BUILD=-mafrlcsj
else
COMPARISON_BUILD=
endif

ifdef SUB_IMAGE_ADDRESS

else
SUB_IMAGE_ADDRESS_COMMAND=
endif

# Object Directory
OBJECTDIR=build/${CND_CONF}/${IMAGE_TYPE}

# Distribution Directory
DISTDIR=dist/${CND_CONF}/${IMAGE_TYPE}

# Source Files Quoted if spaced
SOURCEFILES_QUOTED_IF_SPACED=../libscpi/src/error.c ../libscpi/src/expression.c ../libscpi/src/fifo.c ../libscpi/src/ieee488.c ../libscpi/src/lexer.c ../libscpi/src/minimal.c ../libscpi/src/parser.c ../libscpi/src/units.c ../libscpi/src/utils.c

# Object Files Quoted if spaced
OBJECTFILES_QUOTED_IF_SPACED=${OBJECTDIR}/_ext/2080528684/error.o ${OBJECTDIR}/_ext/2080528684/expression.o ${OBJECTDIR}/_ext/2080528684/fifo.o ${OBJECTDIR}/_ext/2080528684/ieee488.o ${OBJECTDIR}/_ext/2080528684/lexer.o ${OBJECTDIR}/_ext/2080528684/minimal.o ${OBJECTDIR}/_ext/2080528684/parser.o ${OBJECTDIR}/_ext/2080528684/units.o ${OBJECTDIR}/_ext/2080528684/utils.o
POSSIBLE_DEPFILES=${OBJECTDIR}/_ext/2080528684/error.o.d ${OBJECTDIR}/_ext/2080528684/expression.o.d ${OBJECTDIR}/_ext/2080528684/fifo.o.d ${OBJECTDIR}/_ext/2080528684/ieee488.o.d ${OBJECTDIR}/_ext/2080528684/lexer.o.d ${OBJECTDIR}/_ext/2080528684/minimal.o.d ${OBJECTDIR}/_ext/2080528684/parser.o.d ${OBJECTDIR}/_ext/2080528684/units.o.d ${OBJECTDIR}/_ext/2080528684/utils.o.d

# Object Files
OBJECTFILES=${OBJECTDIR}/_ext/2080528684/error.o ${OBJECTDIR}/_ext/2080528684/expression.o ${OBJECTDIR}/_ext/2080528684/fifo.o ${OBJECTDIR}/_ext/2080528684/ieee488.o ${OBJECTDIR}/_ext/2080528684/lexer.o ${OBJECTDIR}/_ext/2080528684/minimal.o ${OBJECTDIR}/_ext/2080528684/parser.o ${OBJECTDIR}/_ext/2080528684/units.o ${OBJECTDIR}/_ext/2080528684/utils.o

# Source Files
SOURCEFILES=../libscpi/src/error.c ../libscpi/src/expression.c ../libscpi/src/fifo.c ../libscpi/src/ieee488.c ../libscpi/src/lexer.c ../libscpi/src/minimal.c ../libscpi/src/parser.c ../libscpi/src/units.c ../libscpi/src/utils.c



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
	${MAKE}  -f nbproject/Makefile-PIC32MZ2048EFM144.mk ${DISTDIR}/libscpi.X.${OUTPUT_SUFFIX}

MP_PROCESSOR_OPTION=32MZ2048EFM144
MP_LINKER_FILE_OPTION=
# ------------------------------------------------------------------------------------
# Rules for buildStep: assemble
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: assembleWithPreprocess
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: compile
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
${OBJECTDIR}/_ext/2080528684/error.o: ../libscpi/src/error.c  .generated_files/flags/PIC32MZ2048EFM144/3dfc4ee43d5cddc9daf4780933554db0003e4224 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/error.o.d" -o ${OBJECTDIR}/_ext/2080528684/error.o ../libscpi/src/error.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/expression.o: ../libscpi/src/expression.c  .generated_files/flags/PIC32MZ2048EFM144/3721a6905d6bc534902a5804354657221ef0e657 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/expression.o.d" -o ${OBJECTDIR}/_ext/2080528684/expression.o ../libscpi/src/expression.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/fifo.o: ../libscpi/src/fifo.c  .generated_files/flags/PIC32MZ2048EFM144/e1aa239c029715da8a6d93a9c5e819ccef3a11eb .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/fifo.o.d" -o ${OBJECTDIR}/_ext/2080528684/fifo.o ../libscpi/src/fifo.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/ieee488.o: ../libscpi/src/ieee488.c  .generated_files/flags/PIC32MZ2048EFM144/2feb147bc14cbfc0f913a78915186002159dc435 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/ieee488.o.d" -o ${OBJECTDIR}/_ext/2080528684/ieee488.o ../libscpi/src/ieee488.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/lexer.o: ../libscpi/src/lexer.c  .generated_files/flags/PIC32MZ2048EFM144/d9a809ac035cb68150c05fd27a3ce191893261de .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/lexer.o.d" -o ${OBJECTDIR}/_ext/2080528684/lexer.o ../libscpi/src/lexer.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/minimal.o: ../libscpi/src/minimal.c  .generated_files/flags/PIC32MZ2048EFM144/2120fdc736fb295bb7a095a805e99150ec35e225 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/minimal.o.d" -o ${OBJECTDIR}/_ext/2080528684/minimal.o ../libscpi/src/minimal.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/parser.o: ../libscpi/src/parser.c  .generated_files/flags/PIC32MZ2048EFM144/749b9ac74e0314f6bac3e8831d8dc4b281e6144b .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/parser.o.d" -o ${OBJECTDIR}/_ext/2080528684/parser.o ../libscpi/src/parser.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/units.o: ../libscpi/src/units.c  .generated_files/flags/PIC32MZ2048EFM144/946c0ed410e5abf3811a10537401085c3eff3dd9 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/units.o.d" -o ${OBJECTDIR}/_ext/2080528684/units.o ../libscpi/src/units.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/utils.o: ../libscpi/src/utils.c  .generated_files/flags/PIC32MZ2048EFM144/3207a89c65045a2c0e0014a8d0ba6299acc6b35 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/utils.o.d" -o ${OBJECTDIR}/_ext/2080528684/utils.o ../libscpi/src/utils.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
else
${OBJECTDIR}/_ext/2080528684/error.o: ../libscpi/src/error.c  .generated_files/flags/PIC32MZ2048EFM144/4c8a5ca2dae3b70fb107e53539c97ef811e7250b .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/error.o.d" -o ${OBJECTDIR}/_ext/2080528684/error.o ../libscpi/src/error.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/expression.o: ../libscpi/src/expression.c  .generated_files/flags/PIC32MZ2048EFM144/bc7ad9385f72cca390e5c4a0dbc21fe1e658fdc9 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/expression.o.d" -o ${OBJECTDIR}/_ext/2080528684/expression.o ../libscpi/src/expression.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/fifo.o: ../libscpi/src/fifo.c  .generated_files/flags/PIC32MZ2048EFM144/64954e2eff216532ced37eb04c3e76bb0d1f1937 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/fifo.o.d" -o ${OBJECTDIR}/_ext/2080528684/fifo.o ../libscpi/src/fifo.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/ieee488.o: ../libscpi/src/ieee488.c  .generated_files/flags/PIC32MZ2048EFM144/ed1f8ee9ffc4de52fc3da93cd2fcb72b964a1457 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/ieee488.o.d" -o ${OBJECTDIR}/_ext/2080528684/ieee488.o ../libscpi/src/ieee488.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/lexer.o: ../libscpi/src/lexer.c  .generated_files/flags/PIC32MZ2048EFM144/60eb8f278f17732647e1f541ad386b0e78e74cbe .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/lexer.o.d" -o ${OBJECTDIR}/_ext/2080528684/lexer.o ../libscpi/src/lexer.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/minimal.o: ../libscpi/src/minimal.c  .generated_files/flags/PIC32MZ2048EFM144/8fcdca61c0c15744305f8891e482e298eceb94a5 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/minimal.o.d" -o ${OBJECTDIR}/_ext/2080528684/minimal.o ../libscpi/src/minimal.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/parser.o: ../libscpi/src/parser.c  .generated_files/flags/PIC32MZ2048EFM144/6489647db1e884ca2b4f8c808daabe0804b1867e .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/parser.o.d" -o ${OBJECTDIR}/_ext/2080528684/parser.o ../libscpi/src/parser.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/units.o: ../libscpi/src/units.c  .generated_files/flags/PIC32MZ2048EFM144/8e0c4036bd5884e7678454d7b0c62a2637a96471 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/units.o.d" -o ${OBJECTDIR}/_ext/2080528684/units.o ../libscpi/src/units.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/utils.o: ../libscpi/src/utils.c  .generated_files/flags/PIC32MZ2048EFM144/6ba9f459801d8aff818f0e498da4c365e7e7f04e .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/utils.o.d" -o ${OBJECTDIR}/_ext/2080528684/utils.o ../libscpi/src/utils.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: compileCPP
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: archive
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
${DISTDIR}/libscpi.X.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk    
	@${MKDIR} ${DISTDIR} 
	${MP_AR} $(MP_EXTRA_AR_PRE)  r ${DISTDIR}/libscpi.X.${OUTPUT_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}    
else
${DISTDIR}/libscpi.X.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk   
	@${MKDIR} ${DISTDIR} 
	${MP_AR} $(MP_EXTRA_AR_PRE)  r ${DISTDIR}/libscpi.X.${OUTPUT_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}    
endif


# Subprojects
.build-subprojects:


# Subprojects
.clean-subprojects:

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r ${OBJECTDIR}
	${RM} -r ${DISTDIR}

# Enable dependency checking
.dep.inc: .depcheck-impl

DEPFILES=$(shell mplabwildcard ${POSSIBLE_DEPFILES})
ifneq (${DEPFILES},)
include ${DEPFILES}
endif
