# The slids library: headers in hdr/, TU sources in src/.
#
# The deliverable is ONE relocatable object, build/lib/libslids.o — every test
# binary links it (from the install, below). It is `ld -r`-merged from the library's per-TU objects
# (string.o, tree.o; more land in LIB_SRCS as the library grows). string.slh and
# tree.slh are template-free, so one object serves every consumer. dump, vector,
# algorithm, and map are TEMPLATE libraries — their sources sit BESIDE their headers in
# hdr/ (`import+` loads the template source sitting next to the header;
# --scan-deps loads every one), and their objects can only be built against a CONSUMER's demand
# pool (--instantiate), so each consumer's Makefile builds them (via the
# slidsc --scan-deps fragments); they are NOT part of libslids.o. The
# library's tests live in test/lib.
#
# THE PACKAGE: the headers live in hdr/slids/, so every importer — lib's own
# sources (-I hdr) and every consumer (-I <install>/include) — spells a module
# `import slids.vector;`, and its identity is `slids.vector` everywhere.
#
# THE INSTALL (build/install) is the library as a consumer outside this tree
# gets it: include/slids/ (every header, and each template library's source
# beside its header), lib/libslids.o, bin/slidsc, and slids.mk (from
# slids-install.mk: how to build against it). Consumers in this tree include
# lib/slids.mk, which runs this Makefile and then includes the installed
# slids.mk. A file is copied only when its source is newer, read-only (an
# edit to an installed copy fails instead of being silently overwritten).
# lib/src is not installed: its TUs ship inside libslids.o.
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

INSTALL_DIR  = ../build/install
INSTALL_HDRS = $(patsubst hdr/%,$(INSTALL_DIR)/include/%,$(wildcard hdr/slids/*.slh hdr/slids/*.sl))
INSTALLED    = $(INSTALL_HDRS) $(INSTALL_DIR)/lib/libslids.o \
               $(INSTALL_DIR)/bin/slidsc $(INSTALL_DIR)/slids.mk

.PHONY: all clean

all: $(INSTALLED)

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

# Each install directory is made ONCE, by its own rule, and the copies wait on
# it (order-only). Not `install -D`: uutils install races creating the same
# directory from parallel copies under -j ("cannot create directory").
$(INSTALL_DIR):
	mkdir -p $@

$(INSTALL_DIR)/include/slids $(INSTALL_DIR)/lib $(INSTALL_DIR)/bin: | $(INSTALL_DIR)
	mkdir -p $@

$(INSTALL_DIR)/include/slids/%: hdr/slids/% | $(INSTALL_DIR)/include/slids
	install -p -m 444 $< $@

$(INSTALL_DIR)/lib/libslids.o: $(LIBSLIDS) | $(INSTALL_DIR)/lib
	install -p -m 444 $< $@

$(INSTALL_DIR)/bin/slidsc: $(SLIDSC) | $(INSTALL_DIR)/bin
	install -p -m 555 $< $@

$(INSTALL_DIR)/slids.mk: slids-install.mk | $(INSTALL_DIR)
	install -p -m 444 $< $@

clean:
	rm -rf $(BUILD_DIR) $(INSTALL_DIR)
