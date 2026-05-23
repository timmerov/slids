SUBDIRS = bugs sample test work

.PHONY: all check regen clean $(SUBDIRS)

all:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d all || exit 1; done

check:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d check || exit 1; done

regen:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d regen; done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done

$(SUBDIRS):
	$(MAKE) -C $@
