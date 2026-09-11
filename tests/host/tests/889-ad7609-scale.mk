# #889: AD7609Scale.h is header-only and dependency-free for exactly the
# same reason FixedPointFmt.h is -- the pure code->volts arithmetic was
# split out of AD7609.h, which is unusable on a host (it pulls in Harmony's
# configuration.h/definitions.h via state/board/AInConfig.h). So: no UUT
# copy, no stubs, and NOT $(INCLUDES) -- this test needs no stub on the
# include path at all, which is the property that makes it worth having.
# test_framework.h resolves relative to the test source's own directory.
# No -lm: the header calls no math functions.

TEST_889_BIN := run_ad7609_tests

$(TEST_889_BIN): test_ad7609_scale.c test_framework.h $(FW_ADC)/AD7609Scale.h $(RECIPES) $(lastword $(MAKEFILE_LIST))
	$(CC) $(CFLAGS) -I$(FW_ADC) -o $(TEST_889_BIN) test_ad7609_scale.c

TESTS += $(TEST_889_BIN)
# Identity, not just a count: the guard in ../Makefile compares the SET of
# claimed sources against the set on disk, so a fragment that is deleted,
# duplicated, or pointed at the wrong source is caught instead of balancing
# out. Keep this naming the source THIS fragment compiles.
TEST_SOURCES_CLAIMED += test_ad7609_scale.c
