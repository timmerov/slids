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

# The library: string.slh on every compile's import path (`import string;` for
# println(String + ...)); every binary links the merged build/lib/libslids.o.
LIB       = ../../lib
LIB_HDR   = $(LIB)/hdr
LIB_BUILD = ../../build/lib
IFLAGS    = -I $(LIB_HDR)

# The runnable programs (each has an exp.<name> golden). `consumer` is the cross-TU
# import test; `math` is a standalone TU binding libm's C functions via `= import`
# blocks — it links `-lm` and has no companion `.sl`. `tmpl_test`/`tmpl_test2` are the
# cross-TU TEMPLATE tests, consuming templates declared in tmpl_lib.slh / tmpl_lib2.slh
# and defined in their same-named sources.
SAMPLES = \
	consumer \
	math \
	tmpl_test \
	tmpl_test2

# The link inputs the import scan cannot derive. The scan finds only TEMPLATE
# sources (tmpl_lib.sl, tmpl_lib2.sl — loaded beside a header that declares
# templates), orders them after their consumers' demand pools, and puts them on
# the link line. A header with no templates names no source file: whichever .sl
# defines its members is the author's choice. So library.sl (library.slh's
# classes and functions) and bird.sl (more of library.slh's members) are
# hand-listed. And extra.sl (extra.slh's Pack, an owner-computed layout) is
# linked into consumer, its one importer.
LIBRARY = $(BUILD_DIR)/library.o
BIRD    = $(BUILD_DIR)/bird.o
EXTRA   = $(BUILD_DIR)/extra.o

# Every file holding negative cases. Unlike the other test dirs this is not just
# $(SAMPLES): a rule about imports has a side per FILE ROLE, and each has to be
# violated where it applies — the consumer, the source that defines the library,
# and the header itself. A .slh case runs through testhdr.sl (see run_negatives.sh),
# which is why that stub is not a SAMPLE and has no golden.
NEGATIVES = \
	consumer.sl \
	library.sl \
	library.slh \
	tmpl_test.sl \
	tmpl_test2.sl \
	tmpl_lib.sl \
	tmpl_lib.slh

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(NEGATIVES)

positives: all
	@../run_positives.sh $(DIR) $(SAMPLES)

# THE IMPORT SCAN (slidsc --scan-deps): each sample's generated fragment wires
# its template-library stage — the template sources it pulls in
# (tmpl_lib, tmpl_lib2), their ordering (each consumer's
# demand pool before a library compiles; tmpl_lib consumes tmpl_lib2, so
# tmpl_lib2 compiles after it), their compile flags (SLIDS_INST), and the
# objects the link needs (<sample>_TMPL_OBJS). Included BEFORE the sample
# rules so the link prerequisites can read the variables; a missing fragment
# is generated and make restarts itself.
ifneq ($(MAKECMDGOALS),clean)
-include $(SAMPLES:%=$(BUILD_DIR)/%.deps.mk)
endif

# $(2): hand link extras (bird); $(3): extra link flags (-lm).
define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1).o $($(1)_TMPL_OBJS) $(2)
	$(CXX) $$^ $(3) -o $$@

$(BUILD_DIR)/$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/$(1).d $(IFLAGS)

$(BUILD_DIR)/$(1).deps.mk: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< --scan-deps $$@ $(IFLAGS)
endef

$(eval $(call SAMPLE_RULES,consumer,$(LIBRARY) $(BIRD) $(EXTRA),))
$(eval $(call SAMPLE_RULES,math,,-lm))
$(eval $(call SAMPLE_RULES,tmpl_test,$(LIBRARY) $(BIRD),))
$(eval $(call SAMPLE_RULES,tmpl_test2,$(LIBRARY) $(BIRD),))

# One .ll -> .o step serves the samples and the library sources alike.
$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# Any OTHER .sl in this directory: the template sources (the fragments set
# SLIDS_INST — `-I . --instantiate <build>` — on the ones that read the demand
# pool) and plain extra TUs like library.sl / bird.sl (empty SLIDS_INST).
$(BUILD_DIR)/%.ll: %.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d $(IFLAGS) $(SLIDS_INST)

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported. Editing a header then re-triggers the
# compile automatically — no hand-maintained header list.
-include $(wildcard $(BUILD_DIR)/*.d)

# Every sample links the library (an extra prerequisite onto the template
# rule; $^ in the link recipe picks it up).
$(addprefix $(BIN_DIR)/,$(SAMPLES)): $(LIB_BUILD)/libslids.o

# Built by lib's own Makefile (make.sh order); self-heals a standalone build.
$(LIB_BUILD)/libslids.o:
	$(MAKE) -C $(LIB)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/,$(SAMPLES))
