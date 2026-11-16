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
# linpro is built from main.sl; linpro.sl is the LP library beside linpro.slh).
SAMPLES = \
	linpro
SAMPLE_SRCS = \
	main

# The link inputs the import scan cannot derive. The scan finds only TEMPLATE
# sources (loaded beside a header that declares templates); linpro.slh declares
# none, so linpro.sl (LP's members) is hand-listed. It is also a template
# CONSUMER (Vector methods LP calls), so its demand pool (linpro.sli) must exist
# before any template library compiles.
LINPRO = $(BUILD_DIR)/linpro.o

.PHONY: all clean $(SAMPLES)

all: $(SAMPLES)

# THE IMPORT SCAN (slidsc --scan-deps): each sample's generated fragment wires
# its template-library instantiation stage — the libraries it pulls in, their
# ordering (every consumer's demand pool before a library compiles; a library
# consuming another library orders after it), their compile flags (SLIDS_INST),
# and the extra objects the link needs (<sample>_TMPL_OBJS). Included BEFORE
# the sample rules so the link prerequisites can read the variables; a missing
# fragment is generated and make restarts itself. With several consumers of
# one library, the fragments' ordering lines simply merge.
ifneq ($(MAKECMDGOALS),clean)
-include $(SAMPLE_SRCS:%=$(BUILD_DIR)/%.deps.mk)
endif

# $(1): program name. $(2): source stem. $(3): hand link extras. $(4): extra link flags.
# The hand extras' compiles order before the template libraries (their demands
# join the pool first).
define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(2).o $($(2)_TMPL_OBJS) $(3)
	$(CXX) $$^ $(4) -o $$@

$($(2)_TMPL_OBJS:.o=.ll): $(3:.o=.ll)

$(BUILD_DIR)/$(2).ll: $(2).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/$(2).d $(IFLAGS)

$(BUILD_DIR)/$(2).deps.mk: $(2).sl $(SLIDSC) | $(SLIDS_MK)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< --scan-deps $$@ $(IFLAGS)
endef

$(eval $(call SAMPLE_RULES,linpro,main,$(LINPRO),-lm))

# One .ll -> .o step serves samples and template libraries alike.
$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# Any OTHER .sl in this directory: a sample's own template source (votedata.sl,
# beside votedata.slh). The fragments set its SLIDS_INST and put it on the link.
$(BUILD_DIR)/%.ll: %.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d $(IFLAGS) $(SLIDS_INST)

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
