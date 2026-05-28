CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -Wswitch-enum -MMD -MP
LDFLAGS  = -lstdc++fs

BUILD_DIR = ../build/compiler_v2
BIN_DIR   = ../bin
TARGET    = $(BIN_DIR)/slidsc

SRCS      = main.cpp lex.cpp numeric.cpp grammar.cpp constfold.cpp classify.cpp desugar.cpp optimize.cpp layout.cpp codegen.cpp print.cpp strings.cpp widen.cpp token.cpp parse.cpp ast.cpp diagnostic.cpp
OBJS      = $(addprefix $(BUILD_DIR)/, $(SRCS:.cpp=.o))
DEPS      = $(OBJS:.o=.d)

.PHONY: all clean

all: $(BUILD_DIR) $(BIN_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

-include $(DEPS)
