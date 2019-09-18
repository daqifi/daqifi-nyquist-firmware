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
FINAL_IMAGE=dist/${CND_CONF}/${IMAGE_TYPE}/libscpi.X.${OUTPUT_SUFFIX}
else
IMAGE_TYPE=production
OUTPUT_SUFFIX=a
DEBUGGABLE_SUFFIX=
FINAL_IMAGE=dist/${CND_CONF}/${IMAGE_TYPE}/libscpi.X.${OUTPUT_SUFFIX}
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
	${MAKE}  -f nbproject/Makefile-PIC32MZ2048EFM144.mk dist/${CND_CONF}/${IMAGE_TYPE}/libscpi.X.${OUTPUT_SUFFIX}

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
${OBJECTDIR}/_ext/2080528684/error.o: ../libscpi/src/error.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/error.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG -D__MPLAB_DEBUGGER_ICD3=1  -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/error.o.d" -o ${OBJECTDIR}/_ext/2080528684/error.o ../libscpi/src/error.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/expression.o: ../libscpi/src/expression.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/expression.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG -D__MPLAB_DEBUGGER_ICD3=1  -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/expression.o.d" -o ${OBJECTDIR}/_ext/2080528684/expression.o ../libscpi/src/expression.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/fifo.o: ../libscpi/src/fifo.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/fifo.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG -D__MPLAB_DEBUGGER_ICD3=1  -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/fifo.o.d" -o ${OBJECTDIR}/_ext/2080528684/fifo.o ../libscpi/src/fifo.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/ieee488.o: ../libscpi/src/ieee488.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/ieee488.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG -D__MPLAB_DEBUGGER_ICD3=1  -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/ieee488.o.d" -o ${OBJECTDIR}/_ext/2080528684/ieee488.o ../libscpi/src/ieee488.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/lexer.o: ../libscpi/src/lexer.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/lexer.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG -D__MPLAB_DEBUGGER_ICD3=1  -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/lexer.o.d" -o ${OBJECTDIR}/_ext/2080528684/lexer.o ../libscpi/src/lexer.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/minimal.o: ../libscpi/src/minimal.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/minimal.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG -D__MPLAB_DEBUGGER_ICD3=1  -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/minimal.o.d" -o ${OBJECTDIR}/_ext/2080528684/minimal.o ../libscpi/src/minimal.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/parser.o: ../libscpi/src/parser.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/parser.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG -D__MPLAB_DEBUGGER_ICD3=1  -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/parser.o.d" -o ${OBJECTDIR}/_ext/2080528684/parser.o ../libscpi/src/parser.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/units.o: ../libscpi/src/units.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/units.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG -D__MPLAB_DEBUGGER_ICD3=1  -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/units.o.d" -o ${OBJECTDIR}/_ext/2080528684/units.o ../libscpi/src/units.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/utils.o: ../libscpi/src/utils.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/utils.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG -D__MPLAB_DEBUGGER_ICD3=1  -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/utils.o.d" -o ${OBJECTDIR}/_ext/2080528684/utils.o ../libscpi/src/utils.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
else
${OBJECTDIR}/_ext/2080528684/error.o: ../libscpi/src/error.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/error.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/error.o.d" -o ${OBJECTDIR}/_ext/2080528684/error.o ../libscpi/src/error.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/expression.o: ../libscpi/src/expression.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/expression.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/expression.o.d" -o ${OBJECTDIR}/_ext/2080528684/expression.o ../libscpi/src/expression.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/fifo.o: ../libscpi/src/fifo.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/fifo.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/fifo.o.d" -o ${OBJECTDIR}/_ext/2080528684/fifo.o ../libscpi/src/fifo.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/ieee488.o: ../libscpi/src/ieee488.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/ieee488.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/ieee488.o.d" -o ${OBJECTDIR}/_ext/2080528684/ieee488.o ../libscpi/src/ieee488.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/lexer.o: ../libscpi/src/lexer.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/lexer.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/lexer.o.d" -o ${OBJECTDIR}/_ext/2080528684/lexer.o ../libscpi/src/lexer.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/minimal.o: ../libscpi/src/minimal.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/minimal.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/minimal.o.d" -o ${OBJECTDIR}/_ext/2080528684/minimal.o ../libscpi/src/minimal.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/parser.o: ../libscpi/src/parser.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/parser.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/parser.o.d" -o ${OBJECTDIR}/_ext/2080528684/parser.o ../libscpi/src/parser.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/units.o: ../libscpi/src/units.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/units.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/units.o.d" -o ${OBJECTDIR}/_ext/2080528684/units.o ../libscpi/src/units.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
${OBJECTDIR}/_ext/2080528684/utils.o: ../libscpi/src/utils.c  nbproject/Makefile-${CND_CONF}.mk
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o 
	@${FIXDEPS} "${OBJECTDIR}/_ext/2080528684/utils.o.d" $(SILENT) -rsi ${MP_CC_DIR}../  -c ${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MMD -MF "${OBJECTDIR}/_ext/2080528684/utils.o.d" -o ${OBJECTDIR}/_ext/2080528684/utils.o ../libscpi/src/utils.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD) 
	
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: compileCPP
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: archive
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
dist/${CND_CONF}/${IMAGE_TYPE}/libscpi.X.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk    
	@${MKDIR} dist/${CND_CONF}/${IMAGE_TYPE} 
	${MP_AR} $(MP_EXTRA_AR_PRE)  r dist/${CND_CONF}/${IMAGE_TYPE}/libscpi.X.${OUTPUT_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}    
else
dist/${CND_CONF}/${IMAGE_TYPE}/libscpi.X.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk   
	@${MKDIR} dist/${CND_CONF}/${IMAGE_TYPE} 
	${MP_AR} $(MP_EXTRA_AR_PRE)  r dist/${CND_CONF}/${IMAGE_TYPE}/libscpi.X.${OUTPUT_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}    
endif


# Subprojects
.build-subprojects:


# Subprojects
.clean-subprojects:

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r build/PIC32MZ2048EFM144
	${RM} -r dist/PIC32MZ2048EFM144

# Enable dependency checking
.dep.inc: .depcheck-impl

DEPFILES=$(shell mplabwildcard ${POSSIBLE_DEPFILES})
ifneq (${DEPFILES},)
include ${DEPFILES}
endif
