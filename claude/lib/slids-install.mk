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
# and the rule that builds a TEMPLATE LIBRARY (slids.vector, slids.map, ...)
# into the consumer's BUILD_DIR at its module path (<build>/slids/vector.ll).
# The `slidsc --scan-deps` fragments supply its ordering (after every consumer's
# demand pool) and its flags (SLIDS_INST = `-I . --instantiate <build>`).

SLIDS_ROOT    := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
SLIDSC        ?= $(SLIDS_ROOT)/bin/slidsc
SLIDS_INCLUDE := $(SLIDS_ROOT)/include
SLIDS_LIB     := $(SLIDS_ROOT)/lib/libslids.o
IFLAGS        += -I $(SLIDS_INCLUDE)

$(BUILD_DIR)/%.ll: $(SLIDS_INCLUDE)/%.sl $(SLIDSC)
	mkdir -p $(dir $@)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d $(IFLAGS) $(SLIDS_INST)

# The template libraries' header dependencies (their .d files sit beside them).
-include $(wildcard $(BUILD_DIR)/slids/*.d)
