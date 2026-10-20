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

# Tests for the standard library. The library: string.slh / math.slh / tree.slh /
# dump.slh / vector.slh / algorithm.slh / map.slh live in lib/hdr, and a TEMPLATE
# library's source sits BESIDE its header there (dump.sl, vector.sl,
# algorithm.sl, map.sl — the lexer loads the template source next to an imported
# header). The merged libslids.o lands in build/lib (lib's Makefile owns it —
# make.sh builds lib/ before test/). Consumers here compile with -I so
# `import string;` / `import vector;` resolve.
LIB       = ../../lib
LIB_HDR   = $(LIB)/hdr
LIB_BUILD = ../../build/lib
IFLAGS    = -I $(LIB_HDR)

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
	@../run_negatives.sh algorithm.sl

positives: all
	@../run_positives.sh $(DIR) algorithm string map tree

# A test shares its STEM with the library it exercises, and a template
# library's instantiation lands in this same build directory under the
# library's name (vector.ll). So a test's build files are named t_<name>; its
# binary keeps the plain name (bin/<name>, golden exp.<name>).
#
# THE IMPORT SCAN (slidsc --scan-deps): each test's generated fragment wires
# its template-library instantiation stage — the libraries it pulls in, their
# ordering (every consumer's demand pool before a library compiles; a library
# consuming another library orders after it), their compile flags (SLIDS_INST),
# and the extra objects the link needs (t_<name>_TMPL_OBJS). Included BEFORE
# the test rules so the link prerequisites can read the variables; a missing
# fragment is generated and make restarts itself.
ifneq ($(MAKECMDGOALS),clean)
-include $(SAMPLES:%=$(BUILD_DIR)/t_%.deps.mk)
endif

define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/t_$(1).o $(t_$(1)_TMPL_OBJS) $(LIB_BUILD)/libslids.o
	$(CXX) $$^ -o $$@

$(BUILD_DIR)/t_$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/t_$(1).d $(IFLAGS)

$(BUILD_DIR)/t_$(1).deps.mk: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< --scan-deps $$@ $(IFLAGS)
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

# One .ll -> .o step serves tests and template libraries alike.
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

# The library's libslids.o is built by lib's own Makefile (make.sh order). This
# rule self-heals a standalone `make -C test/lib` from scratch; it does NOT
# track staleness — lib/ owns rebuilds of its own sources.
$(LIB_BUILD)/libslids.o:
	$(MAKE) -C $(LIB)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/,$(SAMPLES))
