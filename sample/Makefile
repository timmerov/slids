SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BUILD_DIR = ../build/sample/hello
BIN_DIR   = ../bin

.PHONY: all clean hello

all: hello

hello: $(BIN_DIR)/hello

$(BIN_DIR)/hello: $(BUILD_DIR)/hello.o
	$(CXX) $< -o $@

$(BUILD_DIR)/hello.o: $(BUILD_DIR)/hello.ll
	$(LLC) $(LLCFLAGS) $< -o $@

$(BUILD_DIR)/hello.ll: hello.sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $< -o $@

clean:
	rm -f $(BUILD_DIR)/hello.ll $(BUILD_DIR)/hello.o $(BIN_DIR)/hello
