SUBDIRS = lexer

.PHONY: all clean test $(SUBDIRS)

all: $(SUBDIRS)

test: all

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
