# lib/slids.mk — the IN-REPO entry point to the slids library, for every
# consumer Makefile in this tree (test/, examples/, user/). Set BUILD_DIR, then
#     include <path to repo>/lib/slids.mk
#
# It includes the INSTALLED slids.mk (build/install/slids.mk — the same file a
# consumer outside this tree includes) after making lib/ bring itself up to
# date: the include has a FORCE rule running `make -C lib`, and make remakes
# an included makefile before it reads anything else. So the installed
# headers, libslids.o and slidsc are current before any timestamp is checked,
# and a clean tree bootstraps itself. make restarts only when slids.mk itself
# changed.
#
# SLIDSC is the repo's own compiler (bin/slidsc — what `make -C compiler`
# touches); the installed file's `?=` keeps it.
#
# SLIDS_MK names the installed file. A consumer's --scan-deps fragment rule
# orders after it (`| $(SLIDS_MK)`): the scan reads the installed headers, and
# the fragments are remade in the same pass, possibly in parallel.
#
# `make clean` skips all of this (no lib/ build just to delete files).
#
# `make print-bins` skips it too: it prints one line per binary this directory
# links, "<absolute path> <directory>", for make.sh's name-clash check. BINS
# defaults to $(SAMPLES); a Makefile whose binaries are named otherwise sets
# BINS itself. The consumer's --scan-deps fragment include must skip
# print-bins as it skips clean, so listing names never compiles anything.

_slids_goal := $(.DEFAULT_GOAL)

SLIDS_SOURCE  := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
SLIDS_REPO    := $(patsubst %/lib,%,$(SLIDS_SOURCE))
SLIDS_INSTALL := $(SLIDS_REPO)/build/install
SLIDS_MK      := $(SLIDS_INSTALL)/slids.mk
SLIDSC        := $(SLIDS_REPO)/bin/slidsc

ifeq ($(filter clean print-bins,$(MAKECMDGOALS)),)
include $(SLIDS_MK)
endif

BINS ?= $(SAMPLES)

.PHONY: print-bins
print-bins:
	@for b in $(abspath $(addprefix $(BIN_DIR)/,$(BINS))); do echo "$$b $(CURDIR)"; done

$(SLIDS_MK): SLIDS_FORCE
	$(MAKE) -C $(SLIDS_SOURCE)

.PHONY: SLIDS_FORCE
SLIDS_FORCE:

# The rules above must not become the including Makefile's default goal.
.DEFAULT_GOAL := $(_slids_goal)
