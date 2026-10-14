# A failed recipe (slidsc/llc error) removes its target, so a partial or stale
# .ll/.o is never left looking up-to-date on the next build.
.DELETE_ON_ERROR:

SLIDSC    = ../../bin/slidsc
LLC       = llc
LLVM_LINK = llvm-link
OPT       = opt
CXX       = g++
LLCFLAGS  = --filetype=obj --relocation-model=pic

BIN_DIR   = ../../bin
DIR       := $(notdir $(CURDIR))
BUILD_DIR = ../../build/test/$(DIR)

# The library: string.slh on every compile's import path (`import string;` for
# println(String + ...)); every binary links the merged build/lib/libslids.o.
LIB       = ../../lib
LIB_HDR   = $(LIB)/hdr
LIB_BUILD = ../../build/lib
IFLAGS    = -I $(LIB_HDR)

# The runnable programs (each has an exp.<name> golden). `consumer` is the cross-TU
# import test; `math` is a standalone TU binding libm's C functions via `= import`
# blocks — it links `-lm` and has no sibling `.sl`. `tmpl_test`/`tmpl_test2` are the
# cross-TU TEMPLATE tests, consuming templates declared in tmpl_lib.slh / tmpl_lib2.slh
# and defined in their same-named sources.
SAMPLES = \
	consumer \
	math \
	tmpl_test \
	tmpl_test2

# The link inputs the import scan cannot derive: bird.sl is a NON-SIBLING
# source for library.slh — a member declared in a header may be defined in any
# one .sl, so no import names bird as a module. And extra.sl/.slh is a header
# module NOBODY imports — its live build is (deliberately) an almost-empty
# completer for the completer-unfold negatives, and linking it into consumer
# keeps that live TU honest. Both stay hand-listed. (library.sl, tmpl_lib.sl,
# tmpl_lib2.sl are SIBLINGS of imported headers — the scan finds them, orders
# them after their consumers' demand pools, and puts them on the link line.)
BIRD  = $(BUILD_DIR)/bird.o
EXTRA = $(BUILD_DIR)/extra.o

# Every file holding negative cases. Unlike the other test dirs this is not just
# $(SAMPLES): a rule about imports has a side per FILE ROLE, and each has to be
# violated where it applies — the consumer, the sibling that defines the library,
# and the header itself. A .slh case runs through testhdr.sl (see run_negatives.sh),
# which is why that stub is not a SAMPLE and has no golden.
NEGATIVES = \
	consumer.sl \
	library.sl \
	library.slh \
	extra.sl \
	tmpl_test.sl \
	tmpl_test2.sl \
	tmpl_lib.sl \
	tmpl_lib.slh

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(NEGATIVES)

positives: all
	@../run_positives.sh $(DIR) $(SAMPLES)

# THE IMPORT SCAN (slidsc --scan-deps): each sample's generated fragment wires
# its template-library stage — the sibling-sourced modules it pulls in
# (tmpl_lib, tmpl_lib2, library, extra), their ordering (each consumer's
# demand pool before a library compiles; tmpl_lib consumes tmpl_lib2, so
# tmpl_lib2 compiles after it), their compile flags (SLIDS_INST), and the
# objects the link needs (<sample>_TMPL_OBJS). Included BEFORE the sample
# rules so the link prerequisites can read the variables; a missing fragment
# is generated and make restarts itself.
ifneq ($(MAKECMDGOALS),clean)
-include $(SAMPLES:%=$(BUILD_DIR)/%.deps.mk)
endif

# $(2): hand link extras (bird); $(3): extra link flags (-lm).
define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1).o $($(1)_TMPL_OBJS) $(2)
	$(CXX) $$^ $(3) -o $$@

$(BUILD_DIR)/$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/$(1).d --api-list $(BUILD_DIR)/$(1).api $(IFLAGS)

$(BUILD_DIR)/$(1).deps.mk: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< --scan-deps $$@ $(IFLAGS)
endef

$(eval $(call SAMPLE_RULES,consumer,$(BIRD) $(EXTRA),))
$(eval $(call SAMPLE_RULES,math,,-lm))
$(eval $(call SAMPLE_RULES,tmpl_test,$(BIRD),))
$(eval $(call SAMPLE_RULES,tmpl_test2,$(BIRD),))

# THE IR MERGE (phase A of the self-instantiate landing; this directory is the
# spike, the other test dirs still go straight from .ll to .o). Every TU here
# now reaches its object through llvm-link:
#
#   <stem>.ll  the main compile          \
#   <stem>.ill the self-instantiate module > llvm-link -> <stem>.mll -> <stem>.o
#
# The .ill is the module that will hold this TU's LOCAL-TYPE template flavors —
# the ones whose type-list names a class private to this TU, which no other TU
# can spell and whose symbols must end up `internal` (bird.sl's and
# tmpl_test.sl's `Twin` flavors mangle to the IDENTICAL strings and both
# objects are in the tmpl_test binary). Merging at the IR level keeps those
# symbols out of every linkable object: they are global only inside this TU's
# own .mll, between this link and the internalize step that follows it, and
# never in a .o that another TU's symbols could bind to.
#
# They CANNOT be `internal` in the .ill itself. Measured on LLVM 21: an
# `internal` definition does not satisfy another module's `declare`, and
# llvm-link drops it, silently, exit 0 — it also drops every unreferenced
# local and linkonce symbol, which is why a merged object here has fewer
# `t`/`W` entries than a direct llc of the same .ll (no global definition is
# ever lost). So the .ill defines them GLOBAL and the internalize pass demotes
# them, driven by an allow-list of the TU's real exports.
#
# Today the .ill is an EMPTY module, always emitted: one unconditional rule per
# TU, no conditional make logic, and the merge is the no-op the goldens prove
# transparent. The internalize step arrives with the .ill's first real content.
#
# Three distinct extensions on purpose — `%.ll`, `%.ill`, `%.mll` cannot match
# each other's pattern rules, so there is no implicit-rule ambiguity and no
# search for a `<stem>.inst.sl` that does not exist.
# Still the EMPTY module: slidsc's `--instantiate-self` already emits the real one
# (verified standalone — valid IR, and its definition set is a subset of the main
# compile's internal definitions), but wiring it here while the main compile ALSO
# defines those flavors would put two definitions of each body in the merge. It takes
# over this rule when the main compile stops defining them.
#
# When it does, this rule must take $(BUILD_DIR)/%.ll as a PREREQUISITE. That ordering
# is the whole answer to duplicate diagnostics: every diagnostic slidsc has is an
# error, and errors render only on failure, so a clean main compile prints nothing and
# the self stage — which only ever runs after one — can report just what it alone can
# see: a type error inside a flavor body.
$(BUILD_DIR)/%.ill:
	mkdir -p $(BUILD_DIR)
	printf 'target triple = "x86_64-pc-linux-gnu"\n' > $@

$(BUILD_DIR)/%.mll: $(BUILD_DIR)/%.ll $(BUILD_DIR)/%.ill
	$(LLVM_LINK) $^ -S -o $@

# THE INTERNALIZE STEP. `internalize` is an ALLOW-list pass: every definition whose
# symbol is absent from the list is demoted to `internal`. slidsc writes the list
# (`--api-list`, the same invocation that writes the .ll — it decides linkage, so it
# knows the set exactly), which is why `main` and every exported global appear in it;
# the pass preserves NOTHING automatically. An omission is an undefined reference at
# link, never a silent miscompile.
#
# With today's empty .ill the pass has nothing of its own to demote, so it must be a
# no-op — which is exactly the gate: it proves the list is complete against the whole
# suite before the .ill carries anything. Once it does, this is the step that takes the
# flavors back out of the global namespace before they reach an object file.
$(BUILD_DIR)/%.oll: $(BUILD_DIR)/%.mll
	$(OPT) -passes=internalize \
		-internalize-public-api-file=$(BUILD_DIR)/$*.api $< -S -o $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.oll
	$(LLC) $(LLCFLAGS) $< -o $@

# The chained intermediates (.ll, .ill, .mll, .oll) are not deleted after the build —
# they are the inspection surface for this work, as in the other test dirs. An
# always-emitted .ill with no prerequisites also must survive, or every build
# would re-mint it and force a needless re-merge and re-llc.
.SECONDARY:

# Any OTHER .sl in this directory: the sibling library sources (the fragments
# set SLIDS_INST — `-I . --instantiate <build>` — on the ones that read the
# demand pool) and plain extra TUs like bird.sl (empty SLIDS_INST).
$(BUILD_DIR)/%.ll: %.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d --api-list $(BUILD_DIR)/$*.api $(IFLAGS) $(SLIDS_INST)

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported. Editing a header then re-triggers the
# compile automatically — no hand-maintained header list.
-include $(wildcard $(BUILD_DIR)/*.d)

# Every sample links the library (an extra prerequisite onto the template
# rule; $^ in the link recipe picks it up).
$(addprefix $(BIN_DIR)/,$(SAMPLES)): $(LIB_BUILD)/libslids.o

# Built by lib's own Makefile (make.sh order); self-heals a standalone build.
$(LIB_BUILD)/libslids.o:
	$(MAKE) -C $(LIB)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/,$(SAMPLES))
