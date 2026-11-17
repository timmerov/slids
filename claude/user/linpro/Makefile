# A failed recipe (slidsc/llc error) removes its target, so a partial or stale
# .ll/.o is never left looking up-to-date on the next build.
.DELETE_ON_ERROR:

LLC       = llc
CXX       = g++
LLCFLAGS  = --filetype=obj --relocation-model=pic

BIN_DIR   = ../../bin
DIR       := $(notdir $(CURDIR))
BUILD_DIR = ../../build/user/$(DIR)

# The slids library, as installed in build/install: lib/slids.mk brings lib/
# up to date first, then includes the installed slids.mk (SLIDSC, IFLAGS,
# SLIDS_LIB, and the rule that builds a template library here).
include ../../lib/slids.mk

# The runnable programs, and the source stem each is built from (the program
# linpro is built from main.sl; the LP library itself, slids.linpro, ships
# inside libslids.o).
SAMPLES = \
	linpro
SAMPLE_SRCS = \
	main

.PHONY: all clean $(SAMPLES)

all: $(SAMPLES)

# THE IMPORT SCAN (slidsc --scan-deps): each sample's generated fragment wires
# it into this directory's instantiation stage (slids_inst) — the template
# sources it pulls in (slids.vector, slids.algorithm, votedata, ...), compiled
# as ordinary TUs, and the objects the link needs (<src>_TMPL_OBJS, slids_inst.o
# among them). Included BEFORE the sample rules so the link prerequisites can
# read the variables; a missing fragment is generated and make restarts itself.
ifneq ($(MAKECMDGOALS),clean)
-include $(SAMPLE_SRCS:%=$(BUILD_DIR)/%.deps.mk)
endif

# $(1): program name. $(2): source stem. $(3): hand link extras. $(4): extra link flags.
define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(2).o $($(2)_TMPL_OBJS) $(3)
	$(CXX) $$^ $(4) -o $$@

$(BUILD_DIR)/$(2).ll: $(2).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/$(2).d $(IFLAGS)

$(BUILD_DIR)/$(2).deps.mk: $(2).sl $(SLIDSC) | $(SLIDS_MK)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< --scan-deps $$@ $(IFLAGS)
endef

$(eval $(call SAMPLE_RULES,linpro,main,,-lm))

# One .ll -> .o step serves samples and template libraries alike.
$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# Any OTHER .sl in this directory: a sample's own template source (votedata.sl,
# beside votedata.slh), an ordinary compile. The fragments put it on the link.
$(BUILD_DIR)/%.ll: %.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d $(IFLAGS)

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported. Editing a header re-triggers the
# compile automatically — no hand-maintained header list.
-include $(wildcard $(BUILD_DIR)/*.d)

# Every sample links the library (an extra prerequisite onto the template
# rule; $^ in the link recipe picks it up).
$(addprefix $(BIN_DIR)/,$(SAMPLES)): $(SLIDS_LIB)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/,$(SAMPLES))
