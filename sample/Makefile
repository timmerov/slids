SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR   = ../bin
BUILD_DIR = ../build/sample

# --- add new samples here ---
SAMPLES = \
	biz_buzz \
	bug01 bug02 bug03 bug04 bug05 bug06 bug08 bug09 \
	cast \
	chess1 chess2 chess3 \
	counter1 counter2 \
	experiment \
	function1 \
	hello1 hello2 \
	leap_years \
	math1 math2 \
	memcpy \
	move \
	primes1 \
	reference1 reference2 reference3 \
	reverse_bits \
	sizeof \
	string01 string02 string03 string04 string05 \
	string06 string07 string08 string09 string10 \
	string11 string12 string13 string14 string15 \
	string16 string17 string18 \
	switch \
	typeconv \
	type_infer

# these should not compile:
#   bug07 bug10

# --- multi-file samples (don't fit the single-file template) ---
MULTI_SAMPLES = import_test

.PHONY: all clean $(SAMPLES) $(MULTI_SAMPLES)

all: $(SAMPLES) $(MULTI_SAMPLES)

define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1)/$(1).o
	$(CXX) $$< -o $$@

$(BUILD_DIR)/$(1)/$(1).o: $(BUILD_DIR)/$(1)/$(1).ll
	$(LLC) $(LLCFLAGS) $$< -o $$@

$(BUILD_DIR)/$(1)/$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)/$(1)
	$(SLIDSC) $$< -o $$@
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

# --- import_test: two source files linked together ---
import_test: $(BIN_DIR)/import_test

$(BIN_DIR)/import_test: \
		$(BUILD_DIR)/import_test/import_def.o \
		$(BUILD_DIR)/import_test/import_test.o
	$(CXX) $^ -o $@

$(BUILD_DIR)/import_test/import_def.o: $(BUILD_DIR)/import_test/import_def.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/import_test/import_test.o: $(BUILD_DIR)/import_test/import_test.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/import_test/import_def.ll: import_def.sl import_decl.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)/import_test
	$(SLIDSC) $< -o $@

$(BUILD_DIR)/import_test/import_test.ll: import_test.sl import_decl.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)/import_test
	$(SLIDSC) $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/, $(SAMPLES) $(MULTI_SAMPLES))
