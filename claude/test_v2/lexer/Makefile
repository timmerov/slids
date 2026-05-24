SLIDSC    = ../../bin/slidsc

BUILD_DIR = ../../build/test_v2/lexer

SAMPLES = \
	comments \
	importer

.PHONY: all clean negatives $(SAMPLES)

all: $(SAMPLES)

negatives:
	@../run_negatives.sh $(addsuffix .sl,$(SAMPLES))

define SAMPLE_RULES
$(1): $(BUILD_DIR)/$(1).stamp

$(BUILD_DIR)/$(1).stamp: $(1).sl $(SLIDSC)
	mkdir -p $(BUILD_DIR)
	$(SLIDSC) $$< -o $(BUILD_DIR)/$(1).ll
	touch $$@
endef

$(foreach s,$(SAMPLES),$(eval $(call SAMPLE_RULES,$(s))))

# Header dependencies — re-lex importer when its .slh change
$(BUILD_DIR)/importer.stamp: importee1.slh importee2.slh

clean:
	rm -rf $(BUILD_DIR)
