# #164: JSON_StringEscape.h is header-only and dependency-free -- the pure
# byte-escaping logic was split out of JSON_Encoder.c, which is unusable on
# a host (it pulls in state/board/BoardConfig.h -> Harmony's
# configuration.h/definitions.h and the whole board/driver graph). No UUT
# copy, no stubs, and NOT $(INCLUDES).

TEST_164_BIN := run_json_escape_tests

$(TEST_164_BIN): test_json_string_escape.c test_framework.h $(FW_SVC)/JSON_StringEscape.h $(RECIPES) $(lastword $(MAKEFILE_LIST))
	$(CC) $(CFLAGS) -I$(FW_SVC) -o $(TEST_164_BIN) test_json_string_escape.c

TESTS += $(TEST_164_BIN)
