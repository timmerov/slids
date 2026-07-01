SLIDSC    = ../../bin/slidsc
LLC       = llc
CXX       = g++
LLCFLAGS  = --filetype=obj --relocation-model=pic

BIN_DIR   = ../../bin
DIR       := $(notdir $(CURDIR))
BUILD_DIR = ../../build/test_v2/$(DIR)

SAMPLES = \
	construct \
	empty \
	field \
	hoisted \
	incomplete \
	inheritance \
	initialize \
	method \
	nameless \
	namespace_cls \
	nested_cls \
	operator \
	overload_cls \
	refine \
	reopen \
	self \
	virtual

.PHONY: all clean negatives positives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(addsuffix .sl,$(SAMPLES))

positives: all
	@../run_positives.sh $(DIR) $(SAMPLES)

define SAMPLE_RULES
$(1): $(BIN_DIR)/$(1)

$(BIN_DIR)/$(1): $(BUILD_DIR)/$(1).o
	$(CXX) $$< -o $$@

$(BUILD_DIR)/$(1).o: $(BUILD_DIR)/$(1).ll
	$(LLC) $(LLCFLAGS) $$< -o $$@

$(BUILD_DIR)/$(1).ll: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $$@
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(addprefix $(BIN_DIR)/, $(SAMPLES))
