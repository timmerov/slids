# The slids library: headers in hdr/, TU sources in src/.
#
# The deliverable is ONE relocatable object, build/lib/libslids.o — every test
# binary links it (from the install, below). It is `ld -r`-merged from the
# library's per-TU objects (LIB_SRCS: string.o, tree.o, linpro.o) and the
# library's own INSTANTIATION STAGE (slids_inst.o, below). dump, vector,
# algorithm, and map are TEMPLATE libraries — their sources sit BESIDE their
# headers in hdr/ (`import+` loads the template source sitting next to the
# header; --scan-deps loads every one), and each consumer's Makefile builds them
# as ordinary TUs beside its own instantiation stage (via the slidsc --scan-deps
# fragments); they are NOT part of libslids.o. The library's tests live in
# test/lib.
#
# THE LIBRARY'S STAGE: a library TU that uses a template (linpro.sl: Vector,
# algorithm, math) writes a .sli demand pool like any consumer, and the stage
# here builds those instances into slids_inst.o, which ships inside libslids.o.
# A consumer's own stage lists libslids.o as a prerequisite, so it leaves out
# every instance libslids.o already defines and builds only the rest (a method
# the library never called). The template libraries' objects the lib builds
# here (<build>/slids/vector.o, ...) are NOT merged: every consumer compiles its
# own copy of each template source it imports, and a source that uses its own
# flavors defines them in that object — shipping the lib's copy too would
# define them twice. The stage still reads them, so it leaves those flavors to
# the consumer's copy.
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

LIB_SRCS = string tree linpro
LIB_OBJS = $(LIB_SRCS:%=$(BUILD_DIR)/%.o)

# Keep each TU's .ll: named only through its .o, it would be an intermediate
# make deletes after the build and rebuilds on the next run (its .d names it).
.SECONDARY: $(LIB_SRCS:%=$(BUILD_DIR)/%.ll)

LIBSLIDS = $(BUILD_DIR)/libslids.o

INSTALL_DIR  = ../build/install
INSTALL_HDRS = $(patsubst hdr/%,$(INSTALL_DIR)/include/%,$(wildcard hdr/slids/*.slh hdr/slids/*.sl))
INSTALLED    = $(INSTALL_HDRS) $(INSTALL_DIR)/lib/libslids.o \
               $(INSTALL_DIR)/bin/slidsc $(INSTALL_DIR)/slids.mk

.PHONY: all clean

all: $(INSTALLED)

# THE IMPORT SCAN (slidsc --scan-deps): each library TU's generated fragment
# wires it into the library's stage — the template libraries it pulls in and
# slids_inst.o (<src>_TMPL_OBJS), and the stage's prerequisites (the TU's .ll,
# the template libraries' objects). Included BEFORE the merge rule so its
# prerequisite list can read the variables; a missing fragment is generated
# and make restarts itself.
ifneq ($(MAKECMDGOALS),clean)
-include $(LIB_SRCS:%=$(BUILD_DIR)/%.deps.mk)
endif

# One relocatable object from the library TUs' objects plus the stage's (when
# any library TU uses a template). Only slids_inst.o of the scanned extras.
LIB_STAGE = $(filter %/slids_inst.o,$(sort $(foreach s,$(LIB_SRCS),$($(s)_TMPL_OBJS))))

$(LIBSLIDS): $(LIB_OBJS) $(LIB_STAGE)
	$(LD) -r $^ -o $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported, so editing a header re-triggers the
# compile. Sources import their own headers from hdr/ (-I).
$(BUILD_DIR)/%.ll: src/%.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d -I hdr

# A template library a library TU pulls in, built at its MODULE PATH
# (slids.vector -> <build>/slids/vector.ll) as the scan fragments name it: an
# ordinary compile, read by the stage, not merged. (The fragments mark its .ll
# .SECONDARY, so make does not delete it as an intermediate and rebuild it on
# the next run.)
$(BUILD_DIR)/slids/%.ll: hdr/slids/%.sl $(SLIDSC)
	mkdir -p $(dir $@)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/slids/$*.d -I hdr

# THE LIBRARY'S INSTANTIATION STAGE: every library TU's .sli under BUILD_DIR.
# The fragments add the prerequisites; every object among them is passed to the
# filter (the stage leaves out what they define).
$(BUILD_DIR)/slids_inst.ll: $(SLIDSC)
	mkdir -p $(dir $@)
	$(SLIDSC) --instantiate $(BUILD_DIR) -o $@ -I hdr $(filter %.o,$^)

$(BUILD_DIR)/%.deps.mk: src/%.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< --scan-deps $@ -I hdr

-include $(wildcard $(BUILD_DIR)/*.d $(BUILD_DIR)/slids/*.d)

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
