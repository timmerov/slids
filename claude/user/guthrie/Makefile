# A failed recipe (slidsc/llc error) removes its target, so a partial or stale
# .ll/.o is never left looking up-to-date on the next build.
.DELETE_ON_ERROR:

# Build in parallel by default: every TU compiles independently. Only at the
# top level: under a parent make, a forced -j would reset its jobserver.
ifeq ($(MAKELEVEL),0)
MAKEFLAGS += -j$(shell nproc)
endif

LLC       = llc
CXX       = g++
LLCFLAGS  = --filetype=obj --relocation-model=pic
CXXFLAGS  = -std=c++17 -O2 -Wall

BIN_DIR   = ../../bin
DIR       := $(notdir $(CURDIR))
BUILD_DIR = ../../build/user/$(DIR)

# The slids library, as installed in build/install: lib/slids.mk brings lib/
# up to date first, then includes the installed slids.mk (SLIDSC, IFLAGS,
# SLIDS_LIB, and the rule that builds a template library here).
include ../../lib/slids.mk

# Every .sl in the directory is a TU of the one binary — no hand-maintained list.
SL_SRCS = $(wildcard *.sl)
SL_STEMS = $(SL_SRCS:.sl=)
SL_OBJS = $(addprefix $(BUILD_DIR)/,$(SL_SRCS:.sl=.o))

# The one binary, for `make print-bins` (lib/slids.mk).
BINS = guthrie

.PHONY: all clean guthrie

# The .ll files are chained-pattern intermediates; without this make deletes
# them after the build. Keep them for inspection.
.SECONDARY: $(SL_OBJS:.o=.ll)

all: guthrie

guthrie: $(BIN_DIR)/guthrie

# THE IMPORT SCAN (slidsc --scan-deps): every TU's generated fragment wires
# this directory's instantiation stage (slids_inst) — each template library it
# pulls in (compiled as ordinary TUs), the stage's prerequisites (that TU's .ll,
# the libraries' objects; the fragments' lines merge across TUs, so the stage
# waits for EVERY TU), and the objects the link needs (<stem>_TMPL_OBJS,
# slids_inst.o among them).
# Included BEFORE the link rule so its prerequisites can read the variables;
# a missing fragment is generated and make restarts itself.
ifeq ($(filter clean print-bins,$(MAKECMDGOALS)),)
-include $(SL_STEMS:%=$(BUILD_DIR)/%.deps.mk)
endif

# The binary: every .sl object, the UNION of the fragments' template-library
# objects, the native random TU, and the library.
$(BIN_DIR)/guthrie: $(SL_OBJS) \
		$(sort $(foreach s,$(SL_STEMS),$($(s)_TMPL_OBJS))) \
		$(BUILD_DIR)/random.o $(SLIDS_LIB)
	$(CXX) $^ -o $@

# random.cc is a native C++ TU (the RNG wrapper). Only this one .cc builds; the
# other .cc files are the C++ originals the .sl ports came from.
$(BUILD_DIR)/random.o: random.cc
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/%.deps.mk: %.sl $(SLIDSC) | $(SLIDS_MK)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< --scan-deps $@ $(IFLAGS)

# slidsc writes a `.d` alongside each `.ll` (-MF): the make rule naming every
# `.slh` that compile imported. Editing a header re-triggers the compiles that
# read it — no hand-maintained header list. On the first build no `.d` exists
# yet (the wildcard is empty, so -include no-ops); they land after the first
# compile and govern every build after. `clean` wipes them with BUILD_DIR.
$(BUILD_DIR)/%.ll: %.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d $(IFLAGS)

-include $(wildcard $(BUILD_DIR)/*.d)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/guthrie
