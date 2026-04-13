SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR   = ../bin
BUILD_DIR = ../build/work

# import-path: source directory (for .slh files alongside .sl source)
# export-path: build directory (annotated .slh files written here; searched first by import)
IMPORT_PATH = .
EXPORT_PATH = $(BUILD_DIR)

.PHONY: all clean string_test opaque_test

all: $(BIN_DIR)/string_test $(BIN_DIR)/opaque_test

$(BIN_DIR)/string_test: $(BUILD_DIR)/string.o $(BUILD_DIR)/string_test.o
	$(CXX) $^ -o $@

$(BUILD_DIR)/string.o: $(BUILD_DIR)/string.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/string_test.o: $(BUILD_DIR)/string_test.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/string.ll: string.sl string.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ --import-path $(IMPORT_PATH) --export-path $(EXPORT_PATH)

$(BUILD_DIR)/string_test.ll: string_test.sl string.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@ --import-path $(IMPORT_PATH) --export-path $(EXPORT_PATH)

$(BIN_DIR)/opaque_test: $(BUILD_DIR)/opaque.o $(BUILD_DIR)/opaque_test.o
	$(CXX) $^ -o $@

$(BUILD_DIR)/opaque.o: $(BUILD_DIR)/opaque.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/opaque_test.o: $(BUILD_DIR)/opaque_test.ll
	$(LLC) $(LLCFLAGS) $< -o $@

# opaque.sl: uses transport, emits opaque.ll AND $(BUILD_DIR)/opaque.slh (annotated)
$(BUILD_DIR)/opaque.ll $(BUILD_DIR)/opaque.slh: opaque.sl opaque.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) opaque.sl -o $(BUILD_DIR)/opaque.ll --import-path $(IMPORT_PATH) --export-path $(EXPORT_PATH)

# opaque_test.sl: depends on the annotated $(BUILD_DIR)/opaque.slh
$(BUILD_DIR)/opaque_test.ll: opaque_test.sl $(BUILD_DIR)/opaque.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) opaque_test.sl -o $(BUILD_DIR)/opaque_test.ll --import-path $(IMPORT_PATH) --export-path $(EXPORT_PATH)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/string_test $(BIN_DIR)/opaque_test
