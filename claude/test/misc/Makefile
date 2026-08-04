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

# The library headers (lib/hdr) are on every compile's import path; every
# binary links the merged library object build/lib/libslids.o (extra
# prerequisite below).
LIB       = ../../lib
LIB_HDR   = $(LIB)/hdr
LIB_BUILD = ../../build/lib
IFLAGS    = -I $(LIB_HDR)

SAMPLES = \
	alias \
	bug \
	hello \
	macros \
	mutable \
	namespace \
	sizeof

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(addsuffix .sl,$(SAMPLES))

positives: all
	@../run_positives.sh $(DIR) $(SAMPLES)

define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1).o
	$(CXX) $$^ -o $$@

$(BUILD_DIR)/$(1).o: $(BUILD_DIR)/$(1).ll
	$(LLC) $(LLCFLAGS) $$< -o $$@

$(BUILD_DIR)/$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/$(1).d $(IFLAGS)
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

# slidsc writes a `.d` alongside each `.ll` (-MF): the make rule naming every
# `.slh` that compile imported, transitively. Editing a header re-triggers the
# compiles that actually read it — no hand-maintained header list. On the first
# build no `.d` exists yet (the wildcard is empty, so -include no-ops); they land
# after the first compile and govern every build after. `clean` wipes them with
# the rest of BUILD_DIR.
-include $(wildcard $(BUILD_DIR)/*.d)

# Every sample links the library (an extra prerequisite onto the template
# rule; $^ in the link recipe picks it up).
$(addprefix $(BIN_DIR)/,$(SAMPLES)): $(LIB_BUILD)/libslids.o

# THE POST-COMPILE PRE-LINK STAGE (the test/import + test/progress shape). vector is a
# TEMPLATE library: a consumer only DEMANDS a flavor, and the flavor's synthesized hooks
# (`__$ctor` / `__$dtor` / the transfer ops) are emitted ONCE, externally, by the
# library's own TU compiled against the demand pool. So vector.sl compiles AFTER its
# consumer here, reading the .sli slidsc dumps beside the consumer's .ll.
$(BUILD_DIR)/vector.ll: $(LIB_HDR)/vector.sl $(BUILD_DIR)/bug.ll $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/vector.d $(IFLAGS) --instantiate $(BUILD_DIR)

# The library TU is not a SAMPLE, so it needs its own .ll -> .o step (the samples get
# theirs from SAMPLE_RULES).
$(BUILD_DIR)/vector.o: $(BUILD_DIR)/vector.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# dump is the directory's OTHER template library — and vector.sl calls dump
# itself, so dump's TU compiles LAST, after bug's and vector's demand pools
# both exist.
$(BUILD_DIR)/dump.ll: $(LIB_HDR)/dump.sl $(BUILD_DIR)/bug.ll $(BUILD_DIR)/vector.ll $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/dump.d $(IFLAGS) -I . --instantiate $(BUILD_DIR)

$(BUILD_DIR)/dump.o: $(BUILD_DIR)/dump.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# bug is this directory's only consumer of the template libraries, so it alone links
# the aggregated flavors (extra prerequisites; the template rule's `$^` picks them up).
$(BIN_DIR)/bug: $(BUILD_DIR)/vector.o $(BUILD_DIR)/dump.o

# Built by lib's own Makefile (make.sh order); self-heals a standalone build.
$(LIB_BUILD)/libslids.o:
	$(MAKE) -C $(LIB)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/, $(SAMPLES))
