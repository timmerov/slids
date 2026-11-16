# slids.mk — building against an INSTALLED slids library.
#
# lib/Makefile installs this file as <install>/slids.mk beside the library it
# describes:
#   <install>/include/slids/   the headers, and each template library's source
#                              beside its header (a consumer instantiates from it);
#                              imported as `import slids.vector;`
#   <install>/lib/libslids.o   the prebuilt library — put it on every link
#   <install>/bin/slidsc       the compiler
#
# A consumer Makefile sets BUILD_DIR (the template-library rule below names it),
# then includes this file by path:
#     BUILD_DIR = build
#     include /path/to/install/slids.mk
# The file finds its own directory, so the install tree can live anywhere.
#
# It sets:
#   SLIDSC         the compiler (`?=`: set it before the include to use another)
#   SLIDS_INCLUDE  the import root (<install>/include)
#   SLIDS_LIB      libslids.o
#   IFLAGS         -I $(SLIDS_INCLUDE) (appended)
# the rule that builds a TEMPLATE LIBRARY (slids.vector, slids.map, ...) into
# the consumer's BUILD_DIR at its module path (<build>/slids/vector.ll) — an
# ordinary compile — and the rule for the build directory's ONE INSTANTIATION
# STAGE, <build>/slids_inst.ll: `slidsc --instantiate <build>` reads every
# consumer's .sli demand pool there, builds every demanded instance in one
# process, and leaves out what an object on its prerequisite list already
# defines. The `slidsc --scan-deps` fragments supply its prerequisites (each
# consumer's .ll, each template library's .o) and put slids_inst.o on the link
# line (<consumer>_TMPL_OBJS).

SLIDS_ROOT    := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
SLIDSC        ?= $(SLIDS_ROOT)/bin/slidsc
SLIDS_INCLUDE := $(SLIDS_ROOT)/include
SLIDS_LIB     := $(SLIDS_ROOT)/lib/libslids.o
IFLAGS        += -I $(SLIDS_INCLUDE)

$(BUILD_DIR)/%.ll: $(SLIDS_INCLUDE)/%.sl $(SLIDSC)
	mkdir -p $(dir $@)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d $(IFLAGS)

# The consumer's own directory is on the import path (a pool's provenance
# imports may name its headers). Every object prerequisite — the template
# libraries', libslids.o, any a Makefile adds — is passed to the stage.
$(BUILD_DIR)/slids_inst.ll: $(SLIDSC) $(SLIDS_LIB)
	mkdir -p $(dir $@)
	$(SLIDSC) --instantiate $(BUILD_DIR) -o $@ -I . $(IFLAGS) $(filter %.o,$^)

# The template libraries' header dependencies (their .d files sit beside them).
-include $(wildcard $(BUILD_DIR)/slids/*.d)
