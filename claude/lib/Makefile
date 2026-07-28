# The slids library: headers in hdr/, TU sources in src/.
#
# The deliverable is ONE relocatable object, build/lib/libslids.o — every test
# binary links it. It is `ld -r`-merged from the library's per-TU objects
# (string.o today; more land in LIB_OBJS as the library grows). string.slh is
# template-free, so one object serves every consumer. dump is a TEMPLATE
# library — dump.sl sits BESIDE dump.slh in hdr/ (the lexer loads the template
# source sitting next to an imported header), and its object can only be built
# against a CONSUMER's demand pool (--instantiate), so each consumer's Makefile
# compiles it (see test/progress); it is NOT part of libslids.o.
#
# A failed recipe (slidsc/llc error) removes its target, so a partial or stale
# .ll/.o is never left looking up-to-date on the next build.
.DELETE_ON_ERROR:

SLIDSC    = ../bin/slidsc
LLC       = llc
LD        = ld
LLCFLAGS  = --filetype=obj --relocation-model=pic

BUILD_DIR = ../build/lib

LIB_OBJS = $(BUILD_DIR)/string.o

LIBSLIDS = $(BUILD_DIR)/libslids.o

.PHONY: all clean

all: $(LIBSLIDS)

# One relocatable object from all the library's objects.
$(LIBSLIDS): $(LIB_OBJS)
	$(LD) -r $^ -o $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# slidsc writes a `.d` alongside the `.ll` (-MF): the make rule naming every
# `.slh` the source textually imported, so editing a header re-triggers the
# compile. Sources import their own headers from hdr/ (-I).
$(BUILD_DIR)/%.ll: src/%.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ -MF $(BUILD_DIR)/$*.d -I hdr

-include $(wildcard $(BUILD_DIR)/*.d)

clean:
	rm -rf $(BUILD_DIR)
