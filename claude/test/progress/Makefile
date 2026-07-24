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

# The runnable programs (a work-in-progress sandbox). `string_test` and
# `vector_test` exercise their libraries; `galaxy` is the scratch sandbox — it
# consumes all of them (dump + math + string + vector).
SAMPLES = \
	galaxy \
	string_test \
	vector_test

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:

# Only string_test carries a golden. `galaxy` is work in progress — its output
# changes freely, so pinning it would just be churn.
positives: all
	@../run_positives.sh $(DIR) string_test

string_test: $(BIN_DIR)/string_test

# string_test links its own object plus the library's string.o (its sibling definitions).
$(BIN_DIR)/string_test: $(BUILD_DIR)/string_test.o $(BUILD_DIR)/string.o
	$(CXX) $^ -o $@

vector_test: $(BIN_DIR)/vector_test

$(BIN_DIR)/vector_test: $(BUILD_DIR)/vector_test.o $(BUILD_DIR)/vector.o
	$(CXX) $^ -o $@

# galaxy consumes every library: dump + string + vector are slids TUs linked in;
# math.slh's `import { }` block binds libm, so the link needs -lm.
galaxy: $(BIN_DIR)/galaxy

$(BIN_DIR)/galaxy: $(BUILD_DIR)/galaxy.o $(BUILD_DIR)/dump.o \
		$(BUILD_DIR)/string.o $(BUILD_DIR)/vector.o
	$(CXX) $^ -lm -o $@

# THE POST-COMPILE PRE-LINK STAGE (the test/import shape): a template source
# compiles AFTER its consumers, reading the demand pool (each consumer's .sli,
# dumped beside its .ll) with --instantiate, so every demanded flavor is
# emitted once, externally, by the library's own TU. vector has TWO consumers
# (vector_test + galaxy); dump's consumer is galaxy.
$(BUILD_DIR)/vector.ll: vector.sl $(BUILD_DIR)/vector_test.ll \
		$(BUILD_DIR)/galaxy.ll $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/vector.d --instantiate $(BUILD_DIR)

$(BUILD_DIR)/dump.ll: dump.sl $(BUILD_DIR)/galaxy.ll $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/dump.d --instantiate $(BUILD_DIR)

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
