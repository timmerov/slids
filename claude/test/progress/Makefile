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

# The library: string.slh / math.slh / dump.slh / vector.slh live in lib/hdr, and a
# TEMPLATE library's source sits BESIDE its header there (dump.sl, vector.sl — the
# lexer loads the template source next to an imported header). The merged libslids.o
# lands in build/lib (lib's Makefile owns it — make.sh builds lib/ before test/).
# Consumers here compile with -I so `import string;` / `import vector;` resolve.
# vector_test.sl also lives in lib/hdr, beside the library it exercises.
LIB       = ../../lib
LIB_HDR   = $(LIB)/hdr
LIB_BUILD = ../../build/lib
IFLAGS    = -I $(LIB_HDR)

# The runnable programs (a work-in-progress sandbox). `string_test` and
# `vector_test` exercise their libraries; `galaxy` is the scratch sandbox.
SAMPLES = \
	galaxy \
	string_test \
	vector_test

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:

# Only string_test carries a golden. `galaxy` is work in progress — its output
# changes freely, so pinning it would just be churn.
positives: all
	@../run_positives.sh $(DIR) string_test

# THE IMPORT SCAN (slidsc --scan-deps): each sample's generated fragment wires
# its template-library instantiation stage — the libraries it pulls in, their
# ordering (every consumer's demand pool before a library compiles; a library
# consuming another library orders after it), their compile flags (SLIDS_INST),
# and the extra objects the link needs (<sample>_TMPL_OBJS). Included BEFORE
# the sample rules so the link prerequisites can read the variables; a missing
# fragment is generated and make restarts itself. With several consumers of
# one library, the fragments' ordering lines simply merge.
ifneq ($(MAKECMDGOALS),clean)
-include $(SAMPLES:%=$(BUILD_DIR)/%.deps.mk)
endif

# $(2) is the sample's source directory — vector_test.sl lives in lib/hdr
# beside the library it exercises; the others live here.
define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1).o $($(1)_TMPL_OBJS)
	$(CXX) $$^ $(3) -o $$@

$(BUILD_DIR)/$(1).ll: $(2)/$(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/$(1).d $(IFLAGS)

$(BUILD_DIR)/$(1).deps.mk: $(2)/$(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< --scan-deps $$@ $(IFLAGS)
endef

# galaxy's math.slh `import { }` block binds libm — its link takes -lm.
$(eval $(call SAMPLE_RULES,galaxy,.,-lm))
$(eval $(call SAMPLE_RULES,string_test,.,))
$(eval $(call SAMPLE_RULES,vector_test,$(LIB_HDR),))

# One .ll -> .o step serves samples and template libraries alike.
$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# A TEMPLATE LIBRARY out of lib/hdr: the generated fragments supply the
# ordering and the flags (SLIDS_INST = `-I . --instantiate <build>`).
$(BUILD_DIR)/%.ll: $(LIB_HDR)/%.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d $(IFLAGS) $(SLIDS_INST)

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported. Editing a header re-triggers the
# compile automatically — no hand-maintained header list.
-include $(wildcard $(BUILD_DIR)/*.d)

# Every sample links the library (an extra prerequisite onto the template
# rule; $^ in the link recipe picks it up).
$(addprefix $(BIN_DIR)/,$(SAMPLES)): $(LIB_BUILD)/libslids.o

# The library's libslids.o is built by lib's own Makefile (make.sh order). This
# rule self-heals a standalone `make -C test/progress` from scratch; it does
# NOT track staleness — lib/ owns rebuilds of its own sources.
$(LIB_BUILD)/libslids.o:
	$(MAKE) -C $(LIB)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/,$(SAMPLES))
