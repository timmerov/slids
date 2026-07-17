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

# The runnable program (has exp.consumer golden + any negatives).
SAMPLES = \
	consumer

# Every translation unit linked into the consumer binary: the program plus
# each imported library's .sl. `import library;` is a textual include of
# library.slh (declarations only) — the DEFINITIONS live in library.sl, which
# is compiled to its own object and linked in here.
# bird.sl is a NON-SIBLING source for library.slh: a member declared in a header
# may be defined in any one .sl, so a header declaring several classes can give
# each its own file. library.sl still emits Bird's SYNTHESIZED members.
OBJS = \
	consumer \
	library \
	bird

# Every file holding negative cases. Unlike the other test dirs this is not just
# $(SAMPLES): a rule about imports has a side per FILE ROLE, and each has to be
# violated where it applies — the consumer, the sibling that defines the library,
# and the header itself. A .slh case runs through testhdr.sl (see run_negatives.sh),
# which is why that stub is not a SAMPLE and has no golden.
NEGATIVES = \
	consumer.sl \
	library.sl \
	library.slh

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(NEGATIVES)

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
