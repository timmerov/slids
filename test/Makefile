SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR   = ../bin
BUILD_DIR = ../build/test

SAMPLES = \
	blocks \
	cast \
	class \
	comments \
	constructor \
	for \
	function1 \
	incomplete \
	indexop \
	inheritance \
	lvalues \
	macros \
	math1 math2 \
	move \
	mutable \
	nested \
	new_delete \
	operators \
	parsing \
	pointer \
	ranges \
	returnslid \
	sizeof \
	strlitcat \
	shadowing \
	swap \
	switch \
	tuple \
	type_conv \
	type_infer \
	virtual

.PHONY: all clean test update negatives $(SAMPLES)

all: $(SAMPLES)

test: all
	@../run_tests.sh

update: all
	@../run_tests.sh --update

negatives:
	@./run_negatives.sh $(addsuffix .sl,$(SAMPLES))

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
