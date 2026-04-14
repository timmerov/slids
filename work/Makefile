SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR    = ../bin
BUILD_DIR  = ../build/work
ST_DIR     = $(BUILD_DIR)/string_test
TM_DIR     = $(BUILD_DIR)/template
EXPORT_DIR = $(BUILD_DIR)/inc

# import-path: source directory (for .slh files alongside .sl source)
# export-path: build directory (annotated .slh files written here; searched first by import)
IMPORT_PATH = .
EXPORT_PATH = $(EXPORT_DIR)

.PHONY: all clean string_test template

all: string_test template

string_test: $(BIN_DIR)/string_test

# ---- string_test ----

$(BIN_DIR)/string_test: $(ST_DIR)/string.o $(ST_DIR)/string_test.o
	$(CXX) $^ -o $@

$(ST_DIR)/string.o: $(ST_DIR)/string.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(ST_DIR)/string_test.o: $(ST_DIR)/string_test.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(ST_DIR)/string.ll: string.sl string.slh $(SLIDSC)
	mkdir -p $(ST_DIR) $(EXPORT_DIR)
	$(SLIDSC) $< -o $@ --import-path $(IMPORT_PATH) --export-path $(EXPORT_PATH)

$(ST_DIR)/string_test.ll: string_test.sl $(EXPORT_DIR)/string.slh $(SLIDSC)
	mkdir -p $(ST_DIR) $(EXPORT_DIR)
	$(SLIDSC) $< -o $@ --import-path $(IMPORT_PATH) --export-path $(EXPORT_PATH)

# annotated header is produced by compiling string.sl
$(EXPORT_DIR)/string.slh: $(ST_DIR)/string.ll

# ---- template ----

template: $(BIN_DIR)/template

$(BIN_DIR)/template: $(ST_DIR)/string.o $(TM_DIR)/template.o
	$(CXX) $^ -o $@

$(TM_DIR)/template.o: $(TM_DIR)/template.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(TM_DIR)/template.ll: template.sl $(EXPORT_DIR)/string.slh $(SLIDSC)
	mkdir -p $(TM_DIR)
	$(SLIDSC) $< -o $@ --import-path $(IMPORT_PATH) --export-path $(EXPORT_PATH)

# ---- clean ----

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/string_test $(BIN_DIR)/template
