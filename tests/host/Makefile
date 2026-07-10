# Host unit tests for firmware/src/Util/CircularBuffer.c (issue #124)
#
# Plain gcc, no external deps. `make run` builds and runs the suite; a
# non-zero exit (any failing test) fails the build.
#
# CircularBuffer.c uses quoted includes ("Logger.h", "osal/osal.h") that GCC
# resolves relative to the *source file's own directory* first. To let our
# stubs win, we compile a build-time copy of the source placed in this
# directory (CircularBuffer_uut.c) — its quoted includes then fall through to
# -Istubs (Logger + osal stubs) and -I$(FW_UTIL) (the real CircularBuffer.h).
# The copy is regenerated on every build, so it always tracks the real source.

CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O0 -g
FW_UTIL  := ../../firmware/src/Util
INCLUDES := -Istubs -I$(FW_UTIL)

BIN := run_tests
UUT := CircularBuffer_uut.c

$(BIN): test_circularbuffer.c test_framework.h stubs/Logger.h stubs/osal/osal.h $(FW_UTIL)/CircularBuffer.c $(FW_UTIL)/CircularBuffer.h
	cp $(FW_UTIL)/CircularBuffer.c $(UUT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(BIN) test_circularbuffer.c $(UUT)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) $(UUT)

.PHONY: run clean
