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

# The library: headers on every compile's import path; every binary links
# the merged library object build/lib/libslids.o.
LIB       = ../../lib
LIB_BUILD = ../../build/lib
IFLAGS    = -I $(LIB)/hdr

SAMPLES = \
	tmpl_alias \
	tmpl_class \
	tmpl_complete \
	tmpl_function \
	tmpl_method \
	tmpl_nested \
	tmpl_operator

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(addsuffix .sl,$(SAMPLES))

positives: all
	@../run_positives.sh $(DIR) $(SAMPLES)

define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1).o
	$(CXX) $$^ -o $$@

$(BUILD_DIR)/$(1).o: $(BUILD_DIR)/$(1).ll
	$(LLC) $(LLCFLAGS) $$< -o $$@

$(BUILD_DIR)/$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@ -MF $(BUILD_DIR)/$(1).d $(IFLAGS)
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

# slidsc writes a `.d` alongside each `.ll` (-MF): the make rule naming every
# `.slh` that compile imported, transitively. Editing a header re-triggers the
# compiles that actually read it — no hand-maintained header list. On the first
# build no `.d` exists yet (the wildcard is empty, so -include no-ops); they land
# after the first compile and govern every build after. `clean` wipes them with
# the rest of BUILD_DIR.
-include $(wildcard $(BUILD_DIR)/*.d)

# Every sample links the library (an extra prerequisite onto the template
# rule; $^ in the link recipe picks it up).
$(addprefix $(BIN_DIR)/,$(SAMPLES)): $(LIB_BUILD)/libslids.o

# Built by lib's own Makefile (make.sh order); self-heals a standalone build.
$(LIB_BUILD)/libslids.o:
	$(MAKE) -C $(LIB)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/, $(SAMPLES))
