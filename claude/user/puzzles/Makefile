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

# The runnable programs.
SAMPLES = \
	gambler \
	standing

.PHONY: all clean $(SAMPLES)

all: $(SAMPLES)

# THE IMPORT SCAN (slidsc --scan-deps): each sample's generated fragment wires
# it into this directory's instantiation stage (slids_inst) — the template
# libraries it pulls in (compiled as ordinary TUs), the stage's prerequisites
# (the consumer's .ll, the libraries' objects), and the extra objects the link
# needs (<sample>_TMPL_OBJS, slids_inst.o among them). Included BEFORE the
# sample rules so the link prerequisites can read the variables; a missing
# fragment is generated and make restarts itself. With several consumers, the
# fragments' stage prerequisites simply merge.
ifeq ($(filter clean print-bins,$(MAKECMDGOALS)),)
-include $(SAMPLES:%=$(BUILD_DIR)/%.deps.mk)
endif

define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1).o $($(1)_TMPL_OBJS)
	$(CXX) $$^ -o $$@

$(BUILD_DIR)/$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/$(1).d $(IFLAGS)

$(BUILD_DIR)/$(1).deps.mk: $(1).sl $(SLIDSC) | $(SLIDS_MK)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< --scan-deps $$@ $(IFLAGS)
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

# One .ll -> .o step serves samples and template libraries alike.
$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

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
