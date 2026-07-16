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
# each imported library's .sl. `import "library";` is a textual include of
# library.slh (declarations only) — the DEFINITIONS live in library.sl, which
# is compiled to its own object and linked in here.
OBJS = \
	consumer \
	library

# Headers textually included via `import` — a change forces a recompile.
HEADERS = \
	library.slh

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

$(BUILD_DIR)/%.ll: %.sl $(HEADERS) $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/,$(SAMPLES))
