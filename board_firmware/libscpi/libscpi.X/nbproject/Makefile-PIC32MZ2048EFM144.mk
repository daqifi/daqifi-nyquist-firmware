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
${OBJECTDIR}/_ext/2080528684/error.o: ../libscpi/src/error.c  .generated_files/flags/PIC32MZ2048EFM144/9407e3cb7f6aca2e6e9c21c76cb8e7b0fc8576e6 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/error.o.d" -o ${OBJECTDIR}/_ext/2080528684/error.o ../libscpi/src/error.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/expression.o: ../libscpi/src/expression.c  .generated_files/flags/PIC32MZ2048EFM144/c5a9870b70c9eec3be521da42066c419f36898ce .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/expression.o.d" -o ${OBJECTDIR}/_ext/2080528684/expression.o ../libscpi/src/expression.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/fifo.o: ../libscpi/src/fifo.c  .generated_files/flags/PIC32MZ2048EFM144/d9565afb385469f21f95b455058d02f8afe66953 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/fifo.o.d" -o ${OBJECTDIR}/_ext/2080528684/fifo.o ../libscpi/src/fifo.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/ieee488.o: ../libscpi/src/ieee488.c  .generated_files/flags/PIC32MZ2048EFM144/ffd8dfd7ec43bd79a1f3f73ef242e643a5cbeb6 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/ieee488.o.d" -o ${OBJECTDIR}/_ext/2080528684/ieee488.o ../libscpi/src/ieee488.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/lexer.o: ../libscpi/src/lexer.c  .generated_files/flags/PIC32MZ2048EFM144/e5fa379a20d31bd3f1a2ac7cb66d64b6a4fb076f .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/lexer.o.d" -o ${OBJECTDIR}/_ext/2080528684/lexer.o ../libscpi/src/lexer.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/minimal.o: ../libscpi/src/minimal.c  .generated_files/flags/PIC32MZ2048EFM144/6abf69235f6697100c91aa561593b832d5eb1b5c .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/minimal.o.d" -o ${OBJECTDIR}/_ext/2080528684/minimal.o ../libscpi/src/minimal.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/parser.o: ../libscpi/src/parser.c  .generated_files/flags/PIC32MZ2048EFM144/e715e6ba49f944fc36df3a073f59f795935fc6 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/parser.o.d" -o ${OBJECTDIR}/_ext/2080528684/parser.o ../libscpi/src/parser.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/units.o: ../libscpi/src/units.c  .generated_files/flags/PIC32MZ2048EFM144/e6235effb4bf8c4780154e8d14b1759d56ceb74 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/units.o.d" -o ${OBJECTDIR}/_ext/2080528684/units.o ../libscpi/src/units.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/utils.o: ../libscpi/src/utils.c  .generated_files/flags/PIC32MZ2048EFM144/17f9e8e8445a3406bc216376dc87250d03b2ee06 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/utils.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/utils.o.d" -o ${OBJECTDIR}/_ext/2080528684/utils.o ../libscpi/src/utils.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
else
${OBJECTDIR}/_ext/2080528684/error.o: ../libscpi/src/error.c  .generated_files/flags/PIC32MZ2048EFM144/c501af1ff87b89b8f638129d8aea1da77050634c .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/error.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/error.o.d" -o ${OBJECTDIR}/_ext/2080528684/error.o ../libscpi/src/error.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/expression.o: ../libscpi/src/expression.c  .generated_files/flags/PIC32MZ2048EFM144/b8d321c276a84d86e542a40037c6820f7d82346d .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/expression.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/expression.o.d" -o ${OBJECTDIR}/_ext/2080528684/expression.o ../libscpi/src/expression.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/fifo.o: ../libscpi/src/fifo.c  .generated_files/flags/PIC32MZ2048EFM144/749f6fa56bbe95404556d05ccab101f2a4bfd2c9 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/fifo.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/fifo.o.d" -o ${OBJECTDIR}/_ext/2080528684/fifo.o ../libscpi/src/fifo.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/ieee488.o: ../libscpi/src/ieee488.c  .generated_files/flags/PIC32MZ2048EFM144/2009aebd25e6927cc645d2dc8663ae712be97c89 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/ieee488.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/ieee488.o.d" -o ${OBJECTDIR}/_ext/2080528684/ieee488.o ../libscpi/src/ieee488.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/lexer.o: ../libscpi/src/lexer.c  .generated_files/flags/PIC32MZ2048EFM144/112b0888eafa3389465ee740786471118482c219 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/lexer.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/lexer.o.d" -o ${OBJECTDIR}/_ext/2080528684/lexer.o ../libscpi/src/lexer.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/minimal.o: ../libscpi/src/minimal.c  .generated_files/flags/PIC32MZ2048EFM144/44bef7b43580864d5442c1a2ee8d0ef14b236ef7 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/minimal.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/minimal.o.d" -o ${OBJECTDIR}/_ext/2080528684/minimal.o ../libscpi/src/minimal.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/parser.o: ../libscpi/src/parser.c  .generated_files/flags/PIC32MZ2048EFM144/7bbc41489bc2c06a20fc1e49db0afb5f35bd61 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/parser.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/parser.o.d" -o ${OBJECTDIR}/_ext/2080528684/parser.o ../libscpi/src/parser.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/units.o: ../libscpi/src/units.c  .generated_files/flags/PIC32MZ2048EFM144/d05a1cee9baea963dd788e621df3f40a638b3384 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
	@${MKDIR} "${OBJECTDIR}/_ext/2080528684" 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o.d 
	@${RM} ${OBJECTDIR}/_ext/2080528684/units.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -I"../libscpi/inc" -MP -MMD -MF "${OBJECTDIR}/_ext/2080528684/units.o.d" -o ${OBJECTDIR}/_ext/2080528684/units.o ../libscpi/src/units.c    -DXPRJ_PIC32MZ2048EFM144=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2080528684/utils.o: ../libscpi/src/utils.c  .generated_files/flags/PIC32MZ2048EFM144/c3d27c23772191660e9a6727aa3cf7dac9aac9f5 .generated_files/flags/PIC32MZ2048EFM144/98fdd68849e8a18ffcbcc43989dec45413e7b5e2
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
