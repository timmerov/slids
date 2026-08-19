# A failed recipe (slidsc/llc error) removes its target, so a partial or stale
# .ll/.o is never left looking up-to-date on the next build.
.DELETE_ON_ERROR:

SLIDSC    = ../../bin/slidsc
LLC       = llc
CXX       = g++
LLCFLAGS  = --filetype=obj --relocation-model=pic
CXXFLAGS  = -std=c++17 -O2 -Wall

BIN_DIR   = ../../bin
DIR       := $(notdir $(CURDIR))
BUILD_DIR = ../../build/test/$(DIR)

# The library headers (lib/hdr) are on every compile's import path; the binary
# links the merged library object build/lib/libslids.o.
LIB       = ../../lib
LIB_HDR   = $(LIB)/hdr
LIB_BUILD = ../../build/lib
IFLAGS    = -I $(LIB_HDR)

# Every .sl in the directory is a TU of the one binary — no hand-maintained list.
SL_SRCS = $(wildcard *.sl)
SL_STEMS = $(SL_SRCS:.sl=)
SL_OBJS = $(addprefix $(BUILD_DIR)/,$(SL_SRCS:.sl=.o))

.PHONY: all clean guthrie

# The .ll files are chained-pattern intermediates; without this make deletes
# them after the build. Keep them for inspection, as the other test dirs do.
.SECONDARY: $(SL_OBJS:.o=.ll)

all: guthrie

guthrie: $(BIN_DIR)/guthrie

# THE IMPORT SCAN (slidsc --scan-deps): every TU's generated fragment wires
# the template-library instantiation stage — each library it pulls in, the
# ordering (that TU's demand pool before the library compiles; the fragments'
# lines merge across TUs, so a library waits for EVERY consumer), the compile
# flags (SLIDS_INST), and the objects the link needs (<stem>_TMPL_OBJS).
# Included BEFORE the link rule so its prerequisites can read the variables;
# a missing fragment is generated and make restarts itself.
ifneq ($(MAKECMDGOALS),clean)
-include $(SL_STEMS:%=$(BUILD_DIR)/%.deps.mk)
endif

# The binary: every .sl object, the UNION of the fragments' template-library
# objects, the native random TU, and the library.
$(BIN_DIR)/guthrie: $(SL_OBJS) \
		$(sort $(foreach s,$(SL_STEMS),$($(s)_TMPL_OBJS))) \
		$(BUILD_DIR)/random.o $(LIB_BUILD)/libslids.o
	$(CXX) $^ -o $@

# random.cc is a native C++ TU (the RNG wrapper). Only this one .cc builds; the
# other .cc files are the C++ originals the .sl ports came from.
$(BUILD_DIR)/random.o: random.cc random.h
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# A TEMPLATE LIBRARY out of lib/hdr: the generated fragments supply the
# ordering and the flags (SLIDS_INST = `-I . --instantiate <build>` — this
# directory joins the import path for the pool's provenance imports).
$(BUILD_DIR)/%.ll: $(LIB_HDR)/%.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d $(IFLAGS) $(SLIDS_INST)

$(BUILD_DIR)/%.deps.mk: %.sl $(SLIDSC)
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

# Built by lib's own Makefile (make.sh order); self-heals a standalone build.
$(LIB_BUILD)/libslids.o:
	$(MAKE) -C $(LIB)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/guthrie
