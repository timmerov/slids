SLIDSC   = ../bin/slidsc
LLC      = llc
CXX      = g++
LLCFLAGS = --filetype=obj --relocation-model=pic

BIN_DIR   = ../bin
BUILD_DIR = ../build/sample

# --- add new samples here ---
SAMPLES = \
	biz_buzz \
	bug1 \
	chess1 chess2 chess3 \
	counter1 counter2 \
	function1 \
	hello1 hello2 \
	leap_years \
	math1 math2 \
	memcpy \
	move \
	primes1 \
	reference1 reference2 reference3 \
	reverse_bits \
	string01 string02 string03 string04 string05 \
	string06 string07 string08 string09 string10 \
	string11 string12 string13 string14 \
	switch

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
