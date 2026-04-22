SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR    = ../bin
BUILD_DIR  = ../build/work
INC_DIR    = $(BUILD_DIR)/incomplete
ST_DIR     = $(BUILD_DIR)/string_test
TM1_DIR    = $(BUILD_DIR)/template1
TM2_DIR    = $(BUILD_DIR)/template2
TMT_DIR    = $(BUILD_DIR)/template_test
VEC_DIR    = $(BUILD_DIR)/vector1
VEC_INST_SL  = $(VEC_DIR)/__instantiations.sl
VEC_INST_LL  = $(VEC_DIR)/__instantiations.ll
VEC_INST_OBJ = $(VEC_DIR)/__instantiations.o
TMT_INST_SL = $(TMT_DIR)/__instantiations.sl
TMT_INST_LL = $(TMT_DIR)/__instantiations.ll
TMT_INST_OBJ = $(TMT_DIR)/__instantiations.o

IMPORT_PATH = .
SLIDSC_FLAGS = --import-path $(IMPORT_PATH)

.PHONY: all clean incomplete string_test template1 template2 template_test vector1

all: incomplete string_test vector1

# ---- incomplete ----

incomplete: $(BIN_DIR)/incomplete

$(BIN_DIR)/incomplete: $(INC_DIR)/incomplete.o
	$(CXX) $^ -o $@

$(INC_DIR)/incomplete.o: $(INC_DIR)/incomplete.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(INC_DIR)/incomplete.ll: incomplete.sl $(SLIDSC)
	mkdir -p $(INC_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS)

string_test: $(BIN_DIR)/string_test template1 template2 template_test

# ---- string_test ----

$(BIN_DIR)/string_test: $(ST_DIR)/string.o $(ST_DIR)/string_test.o
	$(CXX) $^ -o $@

$(ST_DIR)/string.o: $(ST_DIR)/string.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(ST_DIR)/string_test.o: $(ST_DIR)/string_test.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(ST_DIR)/string.ll: string.sl $(SLIDSC)
	mkdir -p $(ST_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(ST_DIR)/string.d

$(ST_DIR)/string_test.ll: string_test.sl $(SLIDSC)
	mkdir -p $(ST_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(ST_DIR)/string_test.d

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

$(BIN_DIR)/template_test: $(ST_DIR)/string.o $(TMT_DIR)/template_test.o $(TMT_INST_OBJ)
	$(CXX) $^ -o $@

$(TMT_DIR)/template_test.o: $(TMT_DIR)/template_test.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(TMT_DIR)/template_test.ll: template_test.sl template_add.slh template_add.sl $(SLIDSC)
	mkdir -p $(TMT_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(TMT_DIR)/template_test.d

# ---- vector1 ----

vector1: $(BIN_DIR)/vector1

$(BIN_DIR)/vector1: $(ST_DIR)/string.o $(VEC_DIR)/vector1.o $(VEC_INST_OBJ)
	$(CXX) $^ -o $@

$(VEC_DIR)/vector1.o: $(VEC_DIR)/vector1.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(VEC_DIR)/vector1.ll: vector1.sl vector.slh vector.sl $(SLIDSC)
	mkdir -p $(VEC_DIR)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS) -MF $(VEC_DIR)/vector1.d

$(VEC_INST_OBJ): $(VEC_INST_LL)
	$(LLC) $(LLCFLAGS) $< -o $@

$(VEC_INST_LL): $(VEC_INST_SL) $(SLIDSC)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS)

$(VEC_INST_SL): $(wildcard $(VEC_DIR)/*.sli) $(SLIDSC)
	$(SLIDSC) --instantiate $(VEC_DIR) -o $@

# ---- instantiations (per-target) ----

$(TMT_INST_OBJ): $(TMT_INST_LL)
	$(LLC) $(LLCFLAGS) $< -o $@

$(TMT_INST_LL): $(TMT_INST_SL) $(SLIDSC)
	$(SLIDSC) $< -o $@ $(SLIDSC_FLAGS)

$(TMT_INST_SL): $(wildcard $(TMT_DIR)/*.sli) $(SLIDSC)
	$(SLIDSC) --instantiate $(TMT_DIR) -o $@

# ---- auto-generated dependencies ----

-include $(ST_DIR)/string.d
-include $(ST_DIR)/string_test.d
-include $(TM1_DIR)/template1.d
-include $(TM2_DIR)/template2.d
-include $(TMT_DIR)/template_test.d
-include $(VEC_DIR)/vector1.d
-include $(VEC_DIR)/__instantiations.d
-include $(TMT_DIR)/__instantiations.d

# ---- clean ----

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/incomplete $(BIN_DIR)/string_test $(BIN_DIR)/template1 $(BIN_DIR)/template2 $(BIN_DIR)/template_test $(BIN_DIR)/vector1
