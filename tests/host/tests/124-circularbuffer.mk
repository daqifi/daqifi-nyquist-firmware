# #124: CircularBuffer.c. See tests/host/Makefile's header comment for why
# this test needs a build-time UUT copy.

TEST_124_BIN := run_tests
TEST_124_UUT := CircularBuffer_uut.c

$(TEST_124_BIN): test_circularbuffer.c test_framework.h stubs/Logger.h stubs/osal/osal.h $(FW_UTIL)/CircularBuffer.c $(FW_UTIL)/CircularBuffer.h $(RECIPES) $(lastword $(MAKEFILE_LIST))
	cp $(FW_UTIL)/CircularBuffer.c $(TEST_124_UUT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TEST_124_BIN) test_circularbuffer.c $(TEST_124_UUT)

TESTS       += $(TEST_124_BIN)
# Identity, not just a count: the guard in ../Makefile compares the SET of
# claimed sources against the set on disk, so a fragment that is deleted,
# duplicated, or pointed at the wrong source is caught instead of balancing
# out. Keep this naming the source THIS fragment compiles.
TEST_SOURCES_CLAIMED += test_circularbuffer.c
CLEAN_EXTRA += $(TEST_124_UUT)
