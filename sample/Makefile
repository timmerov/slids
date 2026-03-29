SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR  = ../bin

.PHONY: all clean hello math1

all: hello math1

# --- hello ---

hello: $(BIN_DIR)/hello

$(BIN_DIR)/hello: ../build/sample/hello/hello.o
	$(CXX) $< -o $@

../build/sample/hello/hello.o: ../build/sample/hello/hello.ll
	$(LLC) $(LLCFLAGS) $< -o $@

../build/sample/hello/hello.ll: hello.sl $(SLIDSC)
	$(SLIDSC) $< -o $@

# --- math1 ---

math1: $(BIN_DIR)/math1

$(BIN_DIR)/math1: ../build/sample/math1/math1.o
	$(CXX) $< -o $@

../build/sample/math1/math1.o: ../build/sample/math1/math1.ll
	$(LLC) $(LLCFLAGS) $< -o $@

../build/sample/math1/math1.ll: math1.sl $(SLIDSC)
	$(SLIDSC) $< -o $@

# --- clean ---

clean:
	rm -f \
	    ../build/sample/hello/hello.ll ../build/sample/hello/hello.o $(BIN_DIR)/hello \
	    ../build/sample/math1/math1.ll ../build/sample/math1/math1.o $(BIN_DIR)/math1
