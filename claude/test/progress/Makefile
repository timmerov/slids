# A failed recipe (slidsc/llc error) removes its target, so a partial or stale
# .ll/.o is never left looking up-to-date on the next build.
.DELETE_ON_ERROR:

SLIDSC    = ../../bin/slidsc
LLC       = llc
CXX       = g++
LLCFLAGS  = --filetype=obj --relocation-model=pic

BIN_DIR   = ../../bin
DIR       := $(notdir $(CURDIR))
BUILD_DIR = ../../build/test/$(DIR)

# The runnable programs (a work-in-progress sandbox). `demo` and `string_test` are
# each a standalone program that `import string;` and link the library's `string.o`.
SAMPLES = \
	demo \
	string_test

# Every translation unit linked into the demo binary: the program plus each
# imported library's .sl. `import string;` is a textual include of string.slh
# (declarations only) — the DEFINITIONS live in string.sl, its sibling, which is
# compiled to its own object and linked in here.
OBJS = \
	demo \
	string

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:

positives:

demo: $(BIN_DIR)/demo
string_test: $(BIN_DIR)/string_test

$(BIN_DIR)/demo: $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(OBJS)))
	$(CXX) $^ -o $@

# string_test links its own object plus the library's string.o (its sibling definitions).
$(BIN_DIR)/string_test: $(BUILD_DIR)/string_test.o $(BUILD_DIR)/string.o
	$(CXX) $^ -o $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported. Editing a header then re-triggers the
# compile automatically — no hand-maintained header list. On the first build the
# `.d` doesn't exist yet (wildcard is empty, -include no-ops); it lands after the
# first compile and governs every build after.
$(BUILD_DIR)/%.ll: %.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d

-include $(wildcard $(BUILD_DIR)/*.d)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/,$(SAMPLES))
