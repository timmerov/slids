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

# The library: headers on every compile's import path; every binary links
# the merged library object build/lib/libslids.o.
LIB       = ../../lib
LIB_HDR   = $(LIB)/hdr
LIB_BUILD = ../../build/lib
IFLAGS    = -I $(LIB_HDR)

SAMPLES = \
	anon \
	array \
	combined \
	destructure

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(addsuffix .sl,$(SAMPLES))

positives: all
	@../run_positives.sh $(DIR) $(SAMPLES)

# THE IMPORT SCAN (slidsc --scan-deps): each sample's generated fragment wires
# its template-library instantiation stage — which imported modules are
# template libraries, their build ordering (the consumer's demand pool first,
# then library-to-library edges), their compile flags (a target-specific
# SLIDS_INST the generic .ll rule splices in), and the extra objects the link
# needs (<sample>_TMPL_OBJS). Included BEFORE the sample rules so the link
# prerequisites can read the variables. A missing fragment has a rule below,
# so make generates it and RESTARTS itself with full knowledge — the first
# build links right with no hand-maintained library lists. (Guarded so
# `make clean` doesn't generate fragments just to delete them.)
ifneq ($(MAKECMDGOALS),clean)
-include $(SAMPLES:%=$(BUILD_DIR)/%.deps.mk)
endif

define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1).o $($(1)_TMPL_OBJS)
	$(CXX) $$^ -o $$@

$(BUILD_DIR)/$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/$(1).d $(IFLAGS)

$(BUILD_DIR)/$(1).deps.mk: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< --scan-deps $$@ $(IFLAGS)
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

# One .ll -> .o step serves samples and template libraries alike.
$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# A TEMPLATE LIBRARY out of lib/hdr: the generated fragments supply the
# ordering (after its consumers' demand pools) and the flags (SLIDS_INST =
# `-I . --instantiate <build>`); this one rule builds any of them.
$(BUILD_DIR)/%.ll: $(LIB_HDR)/%.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d $(IFLAGS) $(SLIDS_INST)

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

# Built by lib's own Makefile (make.sh order); self-heals a standalone build.
$(LIB_BUILD)/libslids.o:
	$(MAKE) -C $(LIB)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/, $(SAMPLES))
