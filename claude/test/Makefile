
SUBDIRS = \
	assign \
	class \
	constfold \
	expression \
	flow \
	function \
	import \
	lex \
	lib \
	misc \
	pointer \
	template \
	tuple

.PHONY: all clean print-bins test negatives positives $(SUBDIRS) libslids

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

# Every subdirectory's binaries, for make.sh's name-clash check.
print-bins:
	@for d in $(SUBDIRS); do $(MAKE) -s --no-print-directory -C $$d print-bins; done
