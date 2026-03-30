SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR   = ../bin
BUILD_DIR = ../build/sample

# --- add new samples here ---
SAMPLES = hello function1 math1 math2 primes1 reverse_bits

.PHONY: all clean $(SAMPLES)

all: $(SAMPLES)

define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1)/$(1).o
	$(CXX) $$< -o $$@

$(BUILD_DIR)/$(1)/$(1).o: $(BUILD_DIR)/$(1)/$(1).ll
	$(LLC) $(LLCFLAGS) $$< -o $$@

$(BUILD_DIR)/$(1)/$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)/$(1)
	$(SLIDSC) $$< -o $$@
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/, $(SAMPLES))
