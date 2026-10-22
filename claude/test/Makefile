
SUBDIRS = \
	assign \
	class \
	constfold \
	expression \
	flow \
	function \
	guthrie \
	import \
	lex \
	lib \
	misc \
	pointer \
	progress \
	puzzles \
	template \
	tuple

.PHONY: all clean test negatives positives $(SUBDIRS) libslids

all: $(SUBDIRS)

test: all

# The library is brought up to date ONCE before the subdirectories run, so a
# parallel build never has several sub-makes rebuilding lib/ at the same time.
libslids:
	$(MAKE) -C ../lib

$(SUBDIRS): libslids
	$(MAKE) -C $@

negatives: libslids
	@for d in $(SUBDIRS); do $(MAKE) -C $$d negatives; done

positives: libslids
	@for d in $(SUBDIRS); do $(MAKE) -C $$d positives; done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
