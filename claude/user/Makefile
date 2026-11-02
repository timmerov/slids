
SUBDIRS = \
	galaxy \
	guthrie \
	linpro \
	puzzles

.PHONY: all clean test $(SUBDIRS) libslids

all: $(SUBDIRS)

test: all

# The library is brought up to date ONCE before the subdirectories run, so a
# parallel build never has several sub-makes rebuilding lib/ at the same time.
libslids:
	$(MAKE) -C ../lib

$(SUBDIRS): libslids
	$(MAKE) -C $@

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
