SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR   = ../bin
BUILD_DIR = ../build/work

.PHONY: all clean main

all: main

main: $(BIN_DIR)/main

$(BIN_DIR)/main: $(BUILD_DIR)/string.o $(BUILD_DIR)/main.o
	$(CXX) $^ -o $@

$(BUILD_DIR)/string.o: $(BUILD_DIR)/string.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/main.o: $(BUILD_DIR)/main.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/string.ll: string.sl string.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@

$(BUILD_DIR)/main.ll: main.sl string.slh $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/main
