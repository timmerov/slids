SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR   = ../bin
BUILD_DIR = ../build/work

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
	$(SLIDSC) $< -o $@

$(BUILD_DIR)/string_test.ll: string_test.sl string.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@

$(BIN_DIR)/opaque_test: $(BUILD_DIR)/opaque.o $(BUILD_DIR)/opaque_test.o
	$(CXX) $^ -o $@

$(BUILD_DIR)/opaque.o: $(BUILD_DIR)/opaque.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/opaque_test.o: $(BUILD_DIR)/opaque_test.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/opaque.ll: opaque.sl opaque.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@

$(BUILD_DIR)/opaque_test.ll: opaque_test.sl opaque.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/string_test $(BIN_DIR)/opaque_test
