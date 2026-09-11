# #124: CircularBuffer.c. See tests/host/Makefile's header comment for why
# this test needs a build-time UUT copy.

TEST_124_BIN := run_tests
TEST_124_UUT := CircularBuffer_uut.c

$(TEST_124_BIN): test_circularbuffer.c test_framework.h stubs/Logger.h stubs/osal/osal.h $(FW_UTIL)/CircularBuffer.c $(FW_UTIL)/CircularBuffer.h $(RECIPES) $(lastword $(MAKEFILE_LIST))
	cp $(FW_UTIL)/CircularBuffer.c $(TEST_124_UUT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TEST_124_BIN) test_circularbuffer.c $(TEST_124_UUT)

TESTS       += $(TEST_124_BIN)
CLEAN_EXTRA += $(TEST_124_UUT)
