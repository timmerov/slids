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

# The runnable programs (each has an exp.<name> golden). `consumer` is the cross-TU
# import test; `math` is a standalone TU binding libm's C functions via `= import`
# blocks — it links `-lm` and has no sibling `.sl`. `tmpl_test` is the cross-TU
# TEMPLATE test: tmpl_test.sl consumes templates declared in tmpl_lib.slh and
# defined in its same-named source tmpl_lib.sl.
SAMPLES = \
	consumer \
	math \
	tmpl_test \
	tmpl_test2

# Every translation unit linked into the consumer binary: the program plus
# each imported library's .sl. `import library;` is a textual include of
# library.slh (declarations only) — the DEFINITIONS live in library.sl, which
# is compiled to its own object and linked in here.
# bird.sl is a NON-SIBLING source for library.slh: a member declared in a header
# may be defined in any one .sl, so a header declaring several classes can give
# each its own file. library.sl still emits Bird's SYNTHESIZED members.
OBJS = \
	consumer \
	library \
	bird

# Every file holding negative cases. Unlike the other test dirs this is not just
# $(SAMPLES): a rule about imports has a side per FILE ROLE, and each has to be
# violated where it applies — the consumer, the sibling that defines the library,
# and the header itself. A .slh case runs through testhdr.sl (see run_negatives.sh),
# which is why that stub is not a SAMPLE and has no golden.
NEGATIVES = \
	consumer.sl \
	library.sl \
	library.slh \
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

consumer: $(BIN_DIR)/consumer

$(BIN_DIR)/consumer: $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(OBJS)))
	$(CXX) $^ -o $@

# math is a single self-contained TU; its `= import` decls bind libm, so it links -lm.
math: $(BIN_DIR)/math

$(BIN_DIR)/math: $(BUILD_DIR)/math.o
	$(CXX) $^ -lm -o $@

# The cross-TU template programs: each consumer links the template sources,
# plus library's TUs (Box<Bird> / tpick<Bird^> pull in Bird's hooks).
# tmpl_lib2 is consumed by tmpl_lib.sl itself (the mixed-role TU), so both
# binaries carry it.
TMPL_LIBS = \
	tmpl_lib \
	tmpl_lib2 \
	library \
	bird

tmpl_test: $(BIN_DIR)/tmpl_test

$(BIN_DIR)/tmpl_test: $(BUILD_DIR)/tmpl_test.o \
		$(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(TMPL_LIBS)))
	$(CXX) $^ -o $@

tmpl_test2: $(BIN_DIR)/tmpl_test2

$(BIN_DIR)/tmpl_test2: $(BUILD_DIR)/tmpl_test2.o \
		$(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(TMPL_LIBS)))
	$(CXX) $^ -o $@

# THE POST-COMPILE PRE-LINK STAGE: a template source compiles AFTER its
# consumers, reading the demand pool (.sli files) they dumped beside their
# .ll. Each demanded flavor instantiates ONCE (the union across every .sli,
# memoized against the source's own uses) and is emitted external. tmpl_lib
# is ALSO a consumer (of tmpl_lib2), so its compile dumps tmpl_lib.sli and
# tmpl_lib2 compiles last of all.
$(BUILD_DIR)/tmpl_lib.ll: tmpl_lib.sl $(BUILD_DIR)/tmpl_test.ll \
		$(BUILD_DIR)/tmpl_test2.ll $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/tmpl_lib.d --instantiate $(BUILD_DIR)

$(BUILD_DIR)/tmpl_lib2.ll: tmpl_lib2.sl $(BUILD_DIR)/tmpl_lib.ll $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/tmpl_lib2.d --instantiate $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported. Editing a header then re-triggers the
# compile automatically — no hand-maintained header list. On the first build the
# `.d` doesn't exist yet (wildcard is empty, -include no-ops); it lands after the
# first compile and governs every build after.
$(BUILD_DIR)/%.ll: %.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d

-include $(wildcard $(BUILD_DIR)/*.d)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/,$(SAMPLES))
