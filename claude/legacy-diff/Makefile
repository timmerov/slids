SUBDIRS = bugs sample test work

.PHONY: all check regen clean help $(SUBDIRS)

all:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d all || exit 1; done

help:
	@echo "legacy-diff snapshot harness"
	@echo ""
	@echo "Targets:"
	@echo "  make all      build .dump files for every input (default)"
	@echo "  make check    diff every .dump against committed .expected baseline"
	@echo "  make regen    rewrite every .expected from current slidsc"
	@echo "                (only on a baseline commit you trust)"
	@echo "  make clean    remove .dump files (.expected is never touched)"
	@echo ""
	@echo "Workflow:"
	@echo "  1. iterate parser changes; run 'make check' to verify byte-identical output"
	@echo "  2. any diff = stop and investigate"
	@echo "  3. for intentional behavior changes: 'make regen' and review the .expected diff"
	@echo ""
	@echo "Inputs live in bugs/, sample/, test/, work/. Each subdir has its own"
	@echo "Makefile with the same target set; this top-level Makefile recurses into all four."

check:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d check || exit 1; done

regen:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d regen; done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done

$(SUBDIRS):
	$(MAKE) -C $@
