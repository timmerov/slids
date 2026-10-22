# The slids library: headers in hdr/, TU sources in src/.
#
# The deliverable is ONE relocatable object, build/lib/libslids.o — every test
# binary links it. It is `ld -r`-merged from the library's per-TU objects
# (string.o, tree.o; more land in LIB_SRCS as the library grows). string.slh and
# tree.slh are template-free, so one object serves every consumer. dump, vector,
# algorithm, and map are TEMPLATE libraries — their sources sit BESIDE their headers in
# hdr/ (`import+` loads the template source sitting next to the header;
# --scan-deps loads every one), and their objects can only be built against a CONSUMER's demand
# pool (--instantiate), so each consumer's Makefile builds them (via the
# slidsc --scan-deps fragments); they are NOT part of libslids.o. The
# library's tests live in test/lib.
#
# A failed recipe (slidsc/llc error) removes its target, so a partial or stale
# .ll/.o is never left looking up-to-date on the next build.
.DELETE_ON_ERROR:

SLIDSC    = ../bin/slidsc
LLC       = llc
LD        = ld
LLCFLAGS  = --filetype=obj --relocation-model=pic

BUILD_DIR = ../build/lib

LIB_SRCS = string tree
LIB_OBJS = $(LIB_SRCS:%=$(BUILD_DIR)/%.o)

LIBSLIDS = $(BUILD_DIR)/libslids.o

.PHONY: all clean

all: $(LIBSLIDS)

# THE IMPORT SCAN (slidsc --scan-deps): each library TU's generated fragment
# wires any template libraries it pulls in — ordering, SLIDS_INST compile
# flags, and the objects to merge (<src>_TMPL_OBJS; empty today — string
# imports no template library). Included BEFORE the merge rule so its
# prerequisite list can read the variables; a missing fragment is generated
# and make restarts itself.
ifneq ($(MAKECMDGOALS),clean)
-include $(LIB_SRCS:%=$(BUILD_DIR)/%.deps.mk)
endif

# One relocatable object from all the library's objects (plus any scanned
# template-library objects a library TU demands).
$(LIBSLIDS): $(LIB_OBJS) $(sort $(foreach s,$(LIB_SRCS),$($(s)_TMPL_OBJS)))
	$(LD) -r $^ -o $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported, so editing a header re-triggers the
# compile. Sources import their own headers from hdr/ (-I). SLIDS_INST comes
# from a scan fragment when the TU is a demanded template library (none of
# libslids.o's own TUs are today).
$(BUILD_DIR)/%.ll: src/%.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d -I hdr $(SLIDS_INST)

$(BUILD_DIR)/%.deps.mk: src/%.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< --scan-deps $@ -I hdr

-include $(wildcard $(BUILD_DIR)/*.d)

clean:
	rm -rf $(BUILD_DIR)
