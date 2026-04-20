SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR    = ../bin
BUILD_DIR  = ../build/work
ST_DIR     = $(BUILD_DIR)/string_test
TM1_DIR    = $(BUILD_DIR)/template1
TM2_DIR    = $(BUILD_DIR)/template2
TMT_DIR    = $(BUILD_DIR)/template_test
VEC_DIR    = $(BUILD_DIR)/vector
EXPORT_DIR = $(BUILD_DIR)/inc
INST_SL    = $(BUILD_DIR)/__instantiations.sl
INST_LL    = $(BUILD_DIR)/__instantiations.ll
INST_OBJ   = $(BUILD_DIR)/__instantiations.o

# import-path: source directory (for .slh files alongside .sl source)
# export-path: build directory (annotated .slh files written here; searched first by import)
IMPORT_PATH = .
EXPORT_PATH = $(EXPORT_DIR)

SLIDSC_FLAGS = --import-path $(IMPORT_PATH) --export-path $(EXPORT_PATH)

.PHONY: all clean string_test template1 template2 template_test vector

all: string_test

string_test: $(BIN_DIR)/string_test template1 template2 template_test

# ---- string_test ----

$(BIN_DIR)/string_test: $(ST_DIR)/string.o $(ST_DIR)/string_test.o
	$(CXX) $^ -o $@

$(ST_DIR)/string.o: $(ST_DIR)/string.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(ST_DIR)/string_test.o: $(ST_DIR)/string_test.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(ST_DIR)/string.ll: string.sl $(SLIDSC)
	mkdir -p $(ST_DIR) $(EXPORT_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(ST_DIR)/string.d

$(ST_DIR)/string_test.ll: string_test.sl $(SLIDSC)
	mkdir -p $(ST_DIR) $(EXPORT_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(ST_DIR)/string_test.d

# annotated header is produced by compiling string.sl
$(EXPORT_DIR)/string.slh: $(ST_DIR)/string.ll

# ---- template1 ----

template1: $(BIN_DIR)/template1

$(BIN_DIR)/template1: $(ST_DIR)/string.o $(TM1_DIR)/template1.o
	$(CXX) $^ -o $@

$(TM1_DIR)/template1.o: $(TM1_DIR)/template1.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(TM1_DIR)/template1.ll: template1.sl $(SLIDSC)
	mkdir -p $(TM1_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(TM1_DIR)/template1.d

# ---- template2 ----

template2: $(BIN_DIR)/template2

$(BIN_DIR)/template2: $(ST_DIR)/string.o $(TM2_DIR)/template2.o
	$(CXX) $^ -o $@

$(TM2_DIR)/template2.o: $(TM2_DIR)/template2.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(TM2_DIR)/template2.ll: template2.sl $(SLIDSC)
	mkdir -p $(TM2_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(TM2_DIR)/template2.d

# ---- template_test ----

template_test: $(BIN_DIR)/template_test

$(BIN_DIR)/template_test: $(ST_DIR)/string.o $(TMT_DIR)/template_test.o $(INST_OBJ)
	$(CXX) $^ -o $@

$(TMT_DIR)/template_test.o: $(TMT_DIR)/template_test.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(TMT_DIR)/template_test.ll: template_test.sl template_add.slh template_add.sl $(SLIDSC)
	mkdir -p $(TMT_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(TMT_DIR)/template_test.d

# ---- vector ----

vector: $(BIN_DIR)/vector

$(BIN_DIR)/vector: $(VEC_DIR)/vector.o
	$(CXX) $^ -o $@

$(VEC_DIR)/vector.o: $(VEC_DIR)/vector.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(VEC_DIR)/vector.ll: vector.sl $(SLIDSC)
	mkdir -p $(VEC_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(VEC_DIR)/vector.d

# ---- instantiations ----

$(INST_OBJ): $(INST_LL)
	$(LLC) $(LLCFLAGS) $< -o $@

$(INST_LL): $(INST_SL) $(SLIDSC)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS)

# rebuild __instantiations.sl whenever any .sli file changes
$(INST_SL): $(wildcard $(BUILD_DIR)/**/*.sli) $(SLIDSC)
	$(SLIDSC) --instantiate $(BUILD_DIR) -o $@

# ---- auto-generated dependencies ----

-include $(ST_DIR)/string.d
-include $(ST_DIR)/string_test.d
-include $(TM1_DIR)/template1.d
-include $(TM2_DIR)/template2.d
-include $(TMT_DIR)/template_test.d
-include $(VEC_DIR)/vector.d

# ---- clean ----

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/string_test $(BIN_DIR)/template1 $(BIN_DIR)/template2 $(BIN_DIR)/template_test $(BIN_DIR)/string_test $(BIN_DIR)/vector
