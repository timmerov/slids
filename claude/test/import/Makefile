SLIDSC    = ../../bin/slidsc
LLC       = llc
CXX       = g++
LLCFLAGS  = --filetype=obj --relocation-model=pic

BIN_DIR   = ../../bin
DIR       := $(notdir $(CURDIR))
BUILD_DIR = ../../build/test/$(DIR)

# The runnable program (has exp.consumer golden + any negatives).
SAMPLES = \
	consumer

# Every translation unit linked into the consumer binary: the program plus
# each imported library's .sl. `import library;` is a textual include of
# library.slh (declarations only) — the DEFINITIONS live in library.sl, which
# is compiled to its own object and linked in here.
OBJS = \
	consumer \
	library

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(addsuffix .sl,$(SAMPLES))

positives: all
	@../run_positives.sh $(DIR) $(SAMPLES)

consumer: $(BIN_DIR)/consumer

$(BIN_DIR)/consumer: $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(OBJS)))
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
