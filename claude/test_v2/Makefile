
SUBDIRS = \
	lexer \
	parser

.PHONY: all clean test negatives $(SUBDIRS)

all: $(SUBDIRS)

test: all

$(SUBDIRS):
	$(MAKE) -C $@

negatives:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d negatives; done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
