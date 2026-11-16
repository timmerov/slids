# A failed recipe (slidsc/llc error) removes its target, so a partial or stale
# .ll/.o is never left looking up-to-date on the next build.
.DELETE_ON_ERROR:

LLC       = llc
CXX       = g++
LLCFLAGS  = --filetype=obj --relocation-model=pic

BIN_DIR   = ../../bin
DIR       := $(notdir $(CURDIR))
BUILD_DIR = ../../build/test/$(DIR)

# The slids library, as installed in build/install: lib/slids.mk brings lib/
# up to date first, then includes the installed slids.mk (SLIDSC, IFLAGS,
# SLIDS_LIB, and the rule that builds a template library here).
include ../../lib/slids.mk

# One test per library, named after it.
SAMPLES = \
	algorithm \
	map \
	string \
	tree \
	vector

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(addsuffix .sl,$(SAMPLES))

positives: all
	@../run_positives.sh $(DIR) $(SAMPLES)

# A test shares its STEM with the library it exercises, and a template
# library's instantiation lands in this same build directory under the
# library's name (vector.ll). So a test's build files are named t_<name>; its
# binary keeps the plain name (bin/<name>, golden exp.<name>).
#
# THE IMPORT SCAN (slidsc --scan-deps): each test's generated fragment wires
# it into this directory's instantiation stage (slids_inst) — the template
# libraries it pulls in, compiled as ordinary TUs, and the extra objects the
# link needs (t_<name>_TMPL_OBJS, slids_inst.o among them). Included BEFORE
# the test rules so the link prerequisites can read the variables; a missing
# fragment is generated and make restarts itself.
ifneq ($(MAKECMDGOALS),clean)
-include $(SAMPLES:%=$(BUILD_DIR)/t_%.deps.mk)
endif

define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/t_$(1).o $(t_$(1)_TMPL_OBJS) $(SLIDS_LIB)
	$(CXX) $$^ -o $$@

$(BUILD_DIR)/t_$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/t_$(1).d $(IFLAGS)

$(BUILD_DIR)/t_$(1).deps.mk: $(1).sl $(SLIDSC) | $(SLIDS_MK)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< --scan-deps $$@ $(IFLAGS)
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

# One .ll -> .o step serves tests and template libraries alike.
$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported. Editing a header re-triggers the
# compile automatically — no hand-maintained header list.
-include $(wildcard $(BUILD_DIR)/*.d)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/,$(SAMPLES))
