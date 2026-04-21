SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR   = ../bin
BUILD_DIR = ../build/bugs

# these should not compile: bug07 bug10 bug12
SAMPLES = \
	bug01 bug02 bug03 bug04 bug05 bug06 bug08 bug09 \
	bug11

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
