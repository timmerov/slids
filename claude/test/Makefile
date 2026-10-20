
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

.PHONY: all clean test negatives positives $(SUBDIRS)

all: $(SUBDIRS)

test: all

$(SUBDIRS):
	$(MAKE) -C $@

negatives:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d negatives; done

positives:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d positives; done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
