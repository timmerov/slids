CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -MMD -MP
LDFLAGS  = -lstdc++fs

BUILD_DIR = ../build/compiler
BIN_DIR   = ../bin
TARGET    = $(BIN_DIR)/slidsc

SRCS      = main.cpp lexer.cpp parser.cpp codegen.cpp codegen_expr.cpp codegen_stmt.cpp codegen_template.cpp
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
