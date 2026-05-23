SLIDSC = ../../bin/slidsc

SL_INPUTS = \
	biz_buzz.sl \
	chess1.sl chess2.sl chess3.sl \
	counter1.sl counter2.sl \
	dump.sl \
	error_messages.sl \
	hello1.sl \
	import_def.sl import_test.sl \
	leap_years.sl \
	math.sl \
	memcpy.sl \
	primes1.sl \
	rangedfor.sl \
	reference1.sl reference2.sl reference3.sl \
	reverse_bits.sl \
	sort.sl

SLH_INPUTS = import_decl.slh

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
