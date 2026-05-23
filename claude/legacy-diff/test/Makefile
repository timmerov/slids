SLIDSC = ../../bin/slidsc

SL_INPUTS = \
	alias.sl array.sl blocks.sl cast.sl chain.sl class.sl comments.sl \
	const.sl constructor.sl for.sl function1.sl function_pointers.sl \
	global1.sl global2.sl headerfields.sl incomplete.sl indexop.sl \
	inheritance.sl initialization.sl instantiate.sl lexer.sl logicops.sl \
	lvalues.sl macros.sl math1.sl misc.sl move.sl mutable.sl namespace.sl \
	nested.sl new_delete.sl opdispatch.sl operators.sl parsing.sl phrases.sl \
	pointer.sl ranges.sl reopen.sl reopen1.sl reopen2.sl returnslid.sl \
	shadowing.sl sizeof.sl strlitcat.sl swap.sl switch.sl tuple.sl \
	type_conv.sl type_infer.sl virtual.sl

SLH_INPUTS = boxhdr.slh

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
