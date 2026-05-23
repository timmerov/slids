SLIDSC = ../../bin/slidsc

SL_INPUTS = \
	dump.sl \
	galaxy.sl \
	string.sl string_test.sl \
	template1.sl template2.sl template_add.sl template_test.sl \
	vector.sl vector1.sl

SLH_INPUTS = \
	dump.slh \
	math.slh \
	string.slh \
	template_add.slh \
	vector.slh

INPUTS = $(SL_INPUTS) $(SLH_INPUTS)
DUMPS  = $(INPUTS:=.dump)

.PHONY: all check regen clean

all: $(DUMPS)

%.sl.dump: %.sl $(SLIDSC)
	$(SLIDSC) --dump-program $< -o $@

%.slh.dump: %.slh $(SLIDSC)
	$(SLIDSC) --dump-program $< -o $@

check: $(DUMPS)
	@fail=0; \
	for f in $(INPUTS); do \
	    if [ ! -f $$f.expected ]; then \
	        echo "MISSING: $$f.expected"; fail=1; \
	    elif ! diff -q $$f.expected $$f.dump > /dev/null; then \
	        echo "DIFF: $$f"; diff $$f.expected $$f.dump; fail=1; \
	    fi; \
	done; \
	if [ $$fail -ne 0 ]; then exit 1; fi; \
	echo "OK ($(words $(INPUTS)) inputs)"

regen:
	@echo "regenerating $(words $(INPUTS)) .expected files from $(SLIDSC)..."
	@for f in $(INPUTS); do \
	    $(SLIDSC) --dump-program $$f -o $$f.expected; \
	done

clean:
	rm -f $(DUMPS)
