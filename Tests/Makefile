# Vox8 standalone tests — framework-free DSP core.
#
# The tests compile ONLY the DSP headers, with no framework on the include
# path. If a test needs JUCE or iPlug2 to build, the core is wrong.
#   make test    build + run the suite (must print ALL TESTS PASSED)
#   make bench   build + run the CPU micro-benchmark (prints numbers)
#   make check   framework-freedom + no-allocation greps over Source/DSP
#   make clean

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -I../Source/DSP
LDFLAGS ?=

TEST_BIN := test_filterbank
BENCH_BIN := bench_filterbank

.PHONY: all test bench check clean

all: test bench

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): test_filterbank.cpp ../Source/DSP/Vox8Filterbank.h \
		../Source/DSP/Vox8Core.h ../Source/DSP/Vox8Params.h
	$(CXX) $(CXXFLAGS) test_filterbank.cpp -o $(TEST_BIN) $(LDFLAGS)

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

$(BENCH_BIN): bench_filterbank.cpp ../Source/DSP/Vox8Filterbank.h \
		../Source/DSP/Vox8Core.h ../Source/DSP/Vox8Params.h
	$(CXX) $(CXXFLAGS) bench_filterbank.cpp -o $(BENCH_BIN) $(LDFLAGS)

# Framework-freedom: zero framework includes anywhere under Source/DSP.
# No heap allocation in the DSP code headers: no new/malloc/vector/string.
# (The greps are scoped to *.h — the .md docs mention framework names in
# prose, which is not an include.)
check:
	@echo "-- framework includes in Source/DSP (must be 0):"
	@! grep -rInE '#include.*(juce|iplug|iPlug|JUCE|IPLUG|vst3|AudioUnit)' \
		../Source/DSP/*.h || (echo "FRAMEWORK INCLUDE FOUND"; exit 1)
	@echo "   0"
	@echo "-- heap allocation in Source/DSP headers (must be 0):"
	@! grep -rInE '\b(new|malloc|calloc|realloc)\b|std::vector|std::string' \
		../Source/DSP/*.h || (echo "HEAP USE FOUND"; exit 1)
	@echo "   0"

clean:
	rm -f $(TEST_BIN) $(BENCH_BIN)
